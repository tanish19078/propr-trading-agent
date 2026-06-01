#pragma once

#include "propr/exec/order_executor.h"
#include "propr/sim/exchange_simulator.h"

namespace propr::sim {

// Thin OrderExecutor adapter so the OrderManager can drive the simulator without
// touching the network. Tests and the --sim mode wire this in instead of
// PropreHttpExecutor.
class SimExecutor final : public exec::OrderExecutor {
 public:
  explicit SimExecutor(ExchangeSimulator& sim) : sim_(sim) {}

  schemas::v1::ExecutionReportV1 place(const schemas::v1::OrderCommandV1& cmd) override {
    return sim_.place(cmd);
  }
  schemas::v1::ExecutionReportV1 cancel_all() override { return sim_.cancel_all(); }
  schemas::v1::ExecutionReportV1 close(const std::string& asset_base,
                                        const std::string& position_side,
                                        core::Qty quantity_nano) override {
    return sim_.close(asset_base, position_side, quantity_nano);
  }

 private:
  ExchangeSimulator& sim_;
};

}  // namespace propr::sim
