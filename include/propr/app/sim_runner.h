#pragma once

#include <cstdint>
#include <string>

#include "propr/config/config.h"

namespace propr::app {

// Offline simulator session: drives the full risk pipeline (strategy plugin ->
// RiskEngine signed commands -> OrderManager -> SimExecutor -> ExchangeSimulator)
// over synthetic seeded price data. No REST, no WS, no API key. This is the
// runtime twin of the simulator drills; use it to watch a strategy live under
// the real risk envelope before any paper/live deployment.
class SimRunner {
 public:
  struct Config {
    int ticks{2000};
    std::uint64_t seed{42};
    std::string strategy_path;  // empty = first enabled entry from RuntimeConfig
    std::string params_path;
  };

  SimRunner(config::RuntimeConfig cfg, Config rc);
  ~SimRunner();

  // Blocking. Returns process exit code.
  int run();

 private:
  config::RuntimeConfig cfg_;
  Config rc_;
};

}  // namespace propr::app
