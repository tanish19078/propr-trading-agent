#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include "propr/account/account.h"
#include "propr/account/challenge_rules.h"
#include "propr/core/clock.h"
#include "propr/core/events.h"
#include "propr/core/types.h"

namespace propr::risk {

// Single global gate. Once armed, the RiskEngine rejects every Intent except
// Intent::Flatten. Reset only after explicit recovery criteria are met.
class KillSwitch {
 public:
  struct Tunables {
    // Floating-loss trip: when (hwm - equity) / max_overall_dd >= threshold_bps.
    int floating_loss_trip_bps{7000};  // 70%
    // Daily-loss trip: when (daily_snapshot - equity) / max_daily_loss >= threshold_bps.
    int daily_loss_trip_bps{7000};
    // WS-disconnect: blind-mode after this many ms.
    int ws_blind_mode_after_ms{5000};
  };

  KillSwitch(Tunables t, const core::Clock& clock)
      : tun_(t), clock_(clock) {}

  bool armed() const { return armed_.load(std::memory_order_acquire); }
  std::string reason() const;

  // Each `check_*` returns the trip event if armed by this call (else nullopt).
  // Idempotent: if already armed for the same reason, returns nullopt.
  // Callers should journal then publish to the EventBus.
  std::optional<core::KillSwitchTrip> check_floating(const account::Account& acct,
                                                     const account::ChallengeRules& rules);
  std::optional<core::KillSwitchTrip> check_daily(const account::Account& acct,
                                                  const account::ChallengeRules& rules,
                                                  core::Money daily_snapshot);
  std::optional<core::KillSwitchTrip> check_ws_disconnect(std::int64_t last_event_ns,
                                                         std::string_view ws_name);
  std::optional<core::KillSwitchTrip> check_reconcile_divergence(
      int consecutive_divergences);

  // Manual reset — used after recovery criteria are met. Returns previous reason.
  std::string reset();

  // For tests.
  void force_arm(core::KillSwitchTrip::Reason r, std::string detail);

 private:
  bool try_arm_(core::KillSwitchTrip::Reason r, std::string detail);

  Tunables tun_;
  const core::Clock& clock_;
  std::atomic<bool> armed_{false};
  mutable std::mutex reason_mu_;
  core::KillSwitchTrip::Reason last_reason_{core::KillSwitchTrip::Reason::ManualHalt};
  std::string last_detail_;
};

}  // namespace propr::risk
