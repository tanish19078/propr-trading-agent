#pragma once

#include <deque>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "propr/account/account.h"
#include "propr/core/clock.h"
#include "propr/core/event_bus.h"
#include "propr/schemas/v1.h"

namespace propr::sim {

// Deterministic in-process exchange simulator. Given the same seed and the same
// sequence of ticks + orders, produces bit-identical outputs. Use it to prove the
// risk core works under hostile conditions BEFORE plugging in a live API.
//
// Knobs (all probability fields are 0.0 .. 1.0):
//   - rejection_probability : place() returns RejectedNotLive (simulating 5xx)
//   - rate_limit_probability: place() returns NetworkError ("429 throttled")
//   - partial_fill_probability : entry fills at 50% of requested qty
//   - fill_skip_probability : place() silently drops the fill (open order limbo)
//   - drop_snapshot_probability : do not publish the next AccountSnapshot
//   - duplicate_snapshot_probability : publish the same snapshot twice
//   - stale_snapshot_ticks : delay publishing snapshots by N ticks
//   - fill_latency_ticks : delay fill by N ticks after place()
//
// Mark/fee/slippage are applied deterministically by computed adjustments.
struct SimConfig {
  std::uint64_t seed{42};
  double rejection_probability{0.0};
  double rate_limit_probability{0.0};
  double partial_fill_probability{0.0};
  double fill_skip_probability{0.0};
  double drop_snapshot_probability{0.0};
  double duplicate_snapshot_probability{0.0};
  int stale_snapshot_ticks{0};
  int fill_latency_ticks{0};
  int taker_fee_bps{8};
  int slippage_bps{5};
  core::Money starting_balance{core::usdc(5000)};
  std::string account_id{"sim_acct_0"};
};

class ExchangeSimulator {
 public:
  ExchangeSimulator(SimConfig cfg, account::Account& mirror,
                    core::EventBus& bus, core::Clock& clock);

  // Drive the simulator forward by one market tick. Marks every open position,
  // releases due delayed fills, may publish AccountSnapshotV1 / PositionUpdateV1.
  void tick(const schemas::v1::TickV1& t);

  // OrderExecutor-style entry points. Called by SimExecutor.
  schemas::v1::ExecutionReportV1 place(const schemas::v1::OrderCommandV1& cmd);
  schemas::v1::ExecutionReportV1 cancel_all();
  schemas::v1::ExecutionReportV1 close(const std::string& asset_base,
                                        const std::string& position_side,
                                        core::Qty quantity_nano);

  // Telemetry.
  core::Money balance() const;
  core::Money equity() const;
  int fills_emitted() const { return fills_emitted_; }
  int rejections_emitted() const { return rejections_emitted_; }

 private:
  struct Holding {
    core::Qty qty{0};
    core::Price avg_entry{0};
    core::Money entry_cost{0};
  };

  struct PendingFill {
    schemas::v1::OrderCommandV1 cmd;
    int releases_in;
    bool partial;
  };

  void release_due_fills_();
  void apply_fill_(const schemas::v1::OrderCommandV1& cmd, core::Qty qty,
                   core::Price mark);
  void publish_account_snapshot_(const char* source);
  void publish_position_update_(const std::string& asset);
  double rng01_();

  SimConfig cfg_;
  account::Account& mirror_;
  core::EventBus& bus_;
  core::Clock& clock_;

  std::mt19937_64 rng_;
  std::unordered_map<std::string, Holding> holdings_;
  std::unordered_map<std::string, core::Price> last_mark_;
  std::vector<PendingFill> pending_;
  std::deque<schemas::v1::AccountSnapshotV1> stale_buffer_;
  core::Money balance_;
  core::Money hwm_;
  int fills_emitted_{0};
  int rejections_emitted_{0};
  mutable std::mutex mu_;
};

}  // namespace propr::sim
