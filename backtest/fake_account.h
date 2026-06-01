#pragma once

// PARKED. This translation unit references the pre-V1 `strategy::Intent` type
// that has been replaced by `schemas::v1::IntentV1`. The whole backtest target
// is excluded from CMake until it is ported. Do not re-enable
// `add_subdirectory(backtest)` until this file builds against the V1 schemas.

#include <unordered_map>
#include <vector>

#include "propr/account/account.h"
#include "propr/core/types.h"
// #include "propr/strategy/intent.h"  // REMOVED - file no longer exists.

namespace propr::backtest {

// Simulates fills, fees, and slippage for offline backtests. Mirrors a Propr account.
class FakeAccount {
 public:
  struct Config {
    core::Money starting_balance{core::usdc(10000.0)};
    int taker_fee_bps{8};       // 0.075% rounded up = 8 bps
    int slippage_bps{5};        // assume 5 bps adverse on every market fill
  };

  FakeAccount(const Config& cfg, account::Account& mirror)
      : cfg_(cfg), mirror_(mirror), balance_(cfg.starting_balance), hwm_(cfg.starting_balance) {
    mirror_.apply_account_update(balance_, 0, 0, 0, hwm_);
  }

  // Simulate filling a single approved-intent entry leg.
  void simulate_entry(const strategy::Intent& intent, core::Price mark);

  // Mark current open positions to a new tick — updates unrealized PnL and HWM.
  void mark_to_tick(const core::Asset& asset, core::Price mark);

  core::Money balance() const { return balance_; }
  core::Money total_unrealized() const { return total_unrealized_; }

 private:
  struct Holding {
    core::Qty qty{0};
    core::Price avg_entry{0};
    core::Money entry_cost{0};
  };

  Config cfg_;
  account::Account& mirror_;
  std::unordered_map<std::string, Holding> holdings_;
  core::Money balance_;
  core::Money total_unrealized_{0};
  core::Money hwm_;
};

}  // namespace propr::backtest
