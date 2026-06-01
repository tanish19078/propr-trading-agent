#pragma once

#include "propr/account/account.h"
#include "propr/account/challenge_rules.h"
#include "propr/core/types.h"

namespace propr::risk {

// Headroom-based sizing. The output is the maximum risk (in micro-USDC) the engine
// will allow on this single intent. The actual qty is then derived from
// risk / (entry - stop).
class SizingPolicy {
 public:
  struct Caps {
    int max_risk_per_trade_bps{200};          // 2.00% of equity
    int max_daily_headroom_use_bps{2500};     // 25% of remaining daily headroom
    int max_overall_headroom_use_bps{1000};   // 10% of remaining overall headroom
  };

  SizingPolicy() = default;
  explicit SizingPolicy(Caps c) : caps_(c) {}

  // Returns the maximum risk allowed, in micro-USDC. Never negative; zero means
  // "do not enter" (reject).
  core::Money max_risk(core::Money equity,
                       core::Money daily_headroom,
                       core::Money overall_headroom) const;

  // Given the per-trade risk budget and a (entry, stop) pair, compute a max qty
  // (in nano-base units) the engine will accept.
  core::Qty max_qty(core::Money risk_budget,
                    core::Price entry,
                    core::Price stop) const;

 private:
  Caps caps_;
};

}  // namespace propr::risk
