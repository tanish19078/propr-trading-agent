#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "propr/core/clock.h"
#include "propr/core/event_bus.h"

namespace ix {
class WebSocket;
}  // namespace ix

namespace propr::net {

// Public market data from Hyperliquid. We subscribe to mark-price channels for the
// assets the agent trades. No auth needed.
class HyperliquidWebSocket {
 public:
  struct Config {
    std::string url;
    std::vector<std::string> subscribe_bases;  // {"BTC", "ETH"}
  };

  HyperliquidWebSocket(Config cfg, core::EventBus& bus, const core::Clock& clock);
  ~HyperliquidWebSocket();

  void start();
  void stop();
  std::int64_t last_event_ns() const { return last_event_ns_.load(std::memory_order_acquire); }

 private:
  void on_open_();
  void handle_text_(const std::string& msg);

  Config cfg_;
  core::EventBus& bus_;
  const core::Clock& clock_;
  std::unique_ptr<ix::WebSocket> ws_;
  std::atomic<std::int64_t> last_event_ns_{0};
  std::atomic<bool> running_{false};
};

}  // namespace propr::net
