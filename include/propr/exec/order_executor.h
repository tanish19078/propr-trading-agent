#pragma once

#include "propr/schemas/v1.h"

namespace propr::exec {

// Interface for "the thing that actually places an order". Two implementations:
//   - PropreHttpExecutor: wraps net::HttpClient, talks to api.propr.xyz.
//   - sim::SimulatorExecutor: in-process simulator with controllable misbehaviour.
//
// OrderManager holds an OrderExecutor*; tests/backtests inject the simulator.
class OrderExecutor {
 public:
  virtual ~OrderExecutor() = default;

  // Submit a verified OrderCommand. Implementations MAY block on network IO.
  virtual schemas::v1::ExecutionReportV1 place(const schemas::v1::OrderCommandV1& cmd) = 0;

  // Cancel everything resting. Uses the reserved-rate path.
  virtual schemas::v1::ExecutionReportV1 cancel_all() = 0;

  // Close a specific asset position. Uses the reserved-rate path.
  virtual schemas::v1::ExecutionReportV1 close(const std::string& asset_base,
                                                const std::string& position_side,
                                                core::Qty quantity_nano) = 0;
};

}  // namespace propr::exec
