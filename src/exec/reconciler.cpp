#include "propr/exec/reconciler.h"

#include <nlohmann/json.hpp>

#include "propr/log/logger.h"

namespace propr::exec {

namespace {
using json = nlohmann::json;
}

Reconciler::Reconciler(net::HttpClient& http,
                       account::Account& account,
                       core::EventBus& bus,
                       risk::KillSwitch& kill,
                       const core::Clock& clock,
                       std::chrono::seconds interval)
    : http_(http),
      account_(account),
      bus_(bus),
      kill_(kill),
      clock_(clock),
      interval_(interval) {}

Reconciler::~Reconciler() { stop(); }

void Reconciler::start() {
  if (running_.exchange(true)) return;
  thread_ = std::thread([this]() { run_(); });
}

void Reconciler::stop() {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
}

void Reconciler::run_() {
  while (running_.load(std::memory_order_acquire)) {
    const bool clean = tick_();
    if (clean) {
      consecutive_.store(0, std::memory_order_release);
    } else {
      const int n = consecutive_.fetch_add(1, std::memory_order_acq_rel) + 1;
      if (auto trip = kill_.check_reconcile_divergence(n)) {
        bus_.publish(*trip);
      }
    }
    for (int i = 0; i < 10 && running_.load(std::memory_order_acquire); ++i) {
      std::this_thread::sleep_for(interval_ / 10);
    }
  }
}

bool Reconciler::tick_() {
  if (account_.id().value.empty()) return true;  // not bootstrapped yet
  const std::string base = "/accounts/" + account_.id().value;

  auto acct = http_.get(base, {});
  if (!acct) {
    PROPR_LOG_WARN(R"({"reconcile_account_fetch_failed":true})");
    bus_.publish(core::ReconcileDivergence{
        .field = core::ReconcileDivergence::Field::Balance,
        .detail = acct.error().message,
        .at_ns = clock_.now_ns(),
    });
    return false;
  }

  // Translate the response into the mirror.
  const json& a = *acct;
  if (a.is_null() || !a.is_object()) return true;
  auto money_from = [](const json& j, const char* k) -> core::Money {
    if (!j.contains(k) || j[k].is_null()) return 0;
    if (j[k].is_string()) {
      try {
        return static_cast<core::Money>(std::stod(j[k].get<std::string>()) *
                                        core::kMicroPerUnit);
      } catch (...) {
        return 0;
      }
    }
    return static_cast<core::Money>(j[k].get<double>() * core::kMicroPerUnit);
  };

  const core::Money balance = money_from(a, "balance");
  const core::Money upnl = money_from(a, "totalUnrealizedPnl");
  const core::Money iso = money_from(a, "isolatedPositionMargin");
  const core::Money xpm = money_from(a, "crossPositionMargin");
  const core::Money hwm = money_from(a, "highWaterMark");

  const core::Money prev_eq = account_.equity();
  account_.apply_account_update(balance, upnl, iso, xpm, hwm);
  const core::Money new_eq = account_.equity();

  // Pure no-op if we matched the mirror; otherwise emit a divergence.
  // We're permissive on small diffs (< 1 micro-USDC), which can happen due to
  // mid-tick WS update racing the REST snapshot.
  const core::Money delta = new_eq > prev_eq ? new_eq - prev_eq : prev_eq - new_eq;
  if (delta > 1) {
    bus_.publish(core::AccountUpdated{
        .balance = balance,
        .total_unrealized_pnl = upnl,
        .isolated_position_margin = iso,
        .cross_position_margin = xpm,
        .high_water_mark = hwm,
        .at_ns = clock_.now_ns(),
    });
  }
  return true;
}

}  // namespace propr::exec
