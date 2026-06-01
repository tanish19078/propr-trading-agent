#include "propr/risk/kill_switch.h"

#include <utility>

namespace propr::risk {

namespace {
constexpr int kBpsScale = 10000;

// Compute consumed_bps = (consumed / total) * 10000, in micro-USDC units. Safe vs zero.
int consumed_bps(core::Money consumed, core::Money total) {
  if (total <= 0 || consumed <= 0) return 0;
  __int128 v = static_cast<__int128>(consumed) * kBpsScale / static_cast<__int128>(total);
  if (v > kBpsScale) v = kBpsScale;
  return static_cast<int>(v);
}

const char* reason_string(core::KillSwitchTrip::Reason r) {
  switch (r) {
    case core::KillSwitchTrip::Reason::FloatingLossExceeded: return "floating_loss";
    case core::KillSwitchTrip::Reason::DailyLossExceeded: return "daily_loss";
    case core::KillSwitchTrip::Reason::WsDisconnected: return "ws_disconnect";
    case core::KillSwitchTrip::Reason::ReconcileDivergence: return "reconcile_divergence";
    case core::KillSwitchTrip::Reason::ManualHalt: return "manual_halt";
  }
  return "unknown";
}
}  // namespace

std::string KillSwitch::reason() const {
  std::lock_guard<std::mutex> g(reason_mu_);
  return std::string(reason_string(last_reason_)) +
         (last_detail_.empty() ? std::string{} : ":" + last_detail_);
}

bool KillSwitch::try_arm_(core::KillSwitchTrip::Reason r, std::string detail) {
  bool expected = false;
  if (!armed_.compare_exchange_strong(expected, true)) {
    return false;  // already armed
  }
  std::lock_guard<std::mutex> g(reason_mu_);
  last_reason_ = r;
  last_detail_ = std::move(detail);
  return true;
}

std::optional<core::KillSwitchTrip> KillSwitch::check_floating(
    const account::Account& acct, const account::ChallengeRules& rules) {
  if (armed()) return std::nullopt;
  const core::Money eq = acct.equity();
  const core::Money hwm = acct.high_water_mark();
  if (hwm <= 0 || rules.max_overall_dd_abs <= 0) return std::nullopt;
  const core::Money consumed = hwm - eq;
  if (consumed <= 0) return std::nullopt;
  if (consumed_bps(consumed, rules.max_overall_dd_abs) < tun_.floating_loss_trip_bps) {
    return std::nullopt;
  }
  std::string detail =
      "consumed=" + std::to_string(consumed) + "/" + std::to_string(rules.max_overall_dd_abs);
  if (!try_arm_(core::KillSwitchTrip::Reason::FloatingLossExceeded, detail)) {
    return std::nullopt;
  }
  return core::KillSwitchTrip{
      .reason = core::KillSwitchTrip::Reason::FloatingLossExceeded,
      .detail = std::move(detail),
      .at_ns = clock_.now_ns(),
  };
}

std::optional<core::KillSwitchTrip> KillSwitch::check_daily(
    const account::Account& acct,
    const account::ChallengeRules& rules,
    core::Money daily_snapshot) {
  if (armed()) return std::nullopt;
  if (rules.max_daily_loss_abs <= 0) return std::nullopt;
  const core::Money eq = acct.equity();
  const core::Money consumed = daily_snapshot - eq;
  if (consumed <= 0) return std::nullopt;
  if (consumed_bps(consumed, rules.max_daily_loss_abs) < tun_.daily_loss_trip_bps) {
    return std::nullopt;
  }
  std::string detail =
      "consumed=" + std::to_string(consumed) + "/" + std::to_string(rules.max_daily_loss_abs);
  if (!try_arm_(core::KillSwitchTrip::Reason::DailyLossExceeded, detail)) {
    return std::nullopt;
  }
  return core::KillSwitchTrip{
      .reason = core::KillSwitchTrip::Reason::DailyLossExceeded,
      .detail = std::move(detail),
      .at_ns = clock_.now_ns(),
  };
}

std::optional<core::KillSwitchTrip> KillSwitch::check_ws_disconnect(
    std::int64_t last_event_ns, std::string_view ws_name) {
  if (armed()) return std::nullopt;
  const std::int64_t now = clock_.now_ns();
  if (last_event_ns <= 0) return std::nullopt;
  const std::int64_t gap_ms = (now - last_event_ns) / 1'000'000;
  if (gap_ms < tun_.ws_blind_mode_after_ms) return std::nullopt;
  std::string detail = std::string(ws_name) + " silent for " + std::to_string(gap_ms) + "ms";
  if (!try_arm_(core::KillSwitchTrip::Reason::WsDisconnected, detail)) {
    return std::nullopt;
  }
  return core::KillSwitchTrip{
      .reason = core::KillSwitchTrip::Reason::WsDisconnected,
      .detail = std::move(detail),
      .at_ns = now,
  };
}

std::optional<core::KillSwitchTrip> KillSwitch::check_reconcile_divergence(
    int consecutive_divergences) {
  if (armed()) return std::nullopt;
  if (consecutive_divergences < 3) return std::nullopt;
  std::string detail = "consecutive=" + std::to_string(consecutive_divergences);
  if (!try_arm_(core::KillSwitchTrip::Reason::ReconcileDivergence, detail)) {
    return std::nullopt;
  }
  return core::KillSwitchTrip{
      .reason = core::KillSwitchTrip::Reason::ReconcileDivergence,
      .detail = std::move(detail),
      .at_ns = clock_.now_ns(),
  };
}

std::string KillSwitch::reset() {
  std::lock_guard<std::mutex> g(reason_mu_);
  armed_.store(false, std::memory_order_release);
  std::string prev = std::string(reason_string(last_reason_));
  last_detail_.clear();
  last_reason_ = core::KillSwitchTrip::Reason::ManualHalt;
  return prev;
}

void KillSwitch::force_arm(core::KillSwitchTrip::Reason r, std::string detail) {
  try_arm_(r, std::move(detail));
}

}  // namespace propr::risk
