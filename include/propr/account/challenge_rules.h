#pragma once

#include <string>

#include "propr/core/types.h"

namespace propr::account {

// Snapshot of the rules for the current challenge attempt. Immutable after init —
// pulled from GET /challenges at startup, never mutated.
struct ChallengeRules {
  std::string challenge_id;
  std::string challenge_name;
  core::Money initial_balance{0};       // micro-USDC
  core::Money profit_target_abs{0};     // absolute, micro-USDC
  core::Money max_overall_dd_abs{0};    // absolute headroom from high-water mark
  core::Money max_daily_loss_abs{0};    // absolute headroom from daily snapshot
  int min_trading_days{0};
  int duration_days{0};                 // 0 = unlimited

  // From the live API: "static" or "trailing". v1 of the risk core only models
  // static drawdown; trailing-DD math requires updating the floor on every new HWM
  // which we do not yet do. Preflight refuses to LIVE if this is "trailing".
  std::string drawdown_type;

  // Computed floors.
  // `dd_floor(hwm)` is the equity level below which the overall DD trips.
  core::Money dd_floor_from(core::Money high_water_mark) const {
    return high_water_mark - max_overall_dd_abs;
  }
  // `daily_floor(snapshot)` is the equity level below which the daily loss trips.
  core::Money daily_floor_from(core::Money daily_snapshot) const {
    return daily_snapshot - max_daily_loss_abs;
  }
};

}  // namespace propr::account
