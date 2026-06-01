#pragma once

#include <optional>
#include <string>

#include "propr/schemas/v1.h"
#include "propr/strategy/market_snapshot.h"

namespace propr::strategy {

// Plugin ABI. Every strategy compiles to a shared library exposing
// `create_strategy` / `destroy_strategy`. Strategies emit schemas::v1::IntentV1
// values; the RiskEngine decides; the OrderManager executes. Strategies have no
// path to the network, no path to the journal, no path to the SDK.
class Strategy {
 public:
  virtual ~Strategy() = default;

  // Stable short string for journals; matches strategies.yaml.
  virtual const char* name() const = 0;

  // Called once after construction. Returns false to abort plugin load.
  virtual bool on_init(const std::string& params_path) = 0;

  // Called on every tick. The MarketSnapshot is read-only.
  virtual std::optional<schemas::v1::IntentV1> on_market(const MarketSnapshot& snap) = 0;

  // Called when one of this strategy's orders fills.
  virtual void on_fill(const schemas::v1::FillV1& fill) = 0;

  // Called when one of this strategy's positions updates / closes.
  virtual void on_position(const schemas::v1::PositionUpdateV1& pos) = 0;

  // Called once before the plugin is unloaded.
  virtual void on_shutdown() = 0;
};

}  // namespace propr::strategy

extern "C" propr::strategy::Strategy* create_strategy();
extern "C" void destroy_strategy(propr::strategy::Strategy*);
