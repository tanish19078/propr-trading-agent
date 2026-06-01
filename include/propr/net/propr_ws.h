#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "propr/core/clock.h"
#include "propr/core/event_bus.h"

namespace ix {
class WebSocket;
}  // namespace ix

namespace propr::net {

// WebSocket client for Propr account-scoped events. Pushes typed Events to the EventBus.
// No replay, no sequence numbers — on every reconnect the App MUST trigger a REST
// reconcile of /accounts/{id}, /positions, /orders.
class ProprWebSocket {
 public:
  struct Config {
    std::string url;
    std::string api_key;
  };

  ProprWebSocket(Config cfg, core::EventBus& bus, const core::Clock& clock);
  ~ProprWebSocket();

  ProprWebSocket(const ProprWebSocket&) = delete;
  ProprWebSocket& operator=(const ProprWebSocket&) = delete;

  void start();
  void stop();

  // Nanos of the most recent message of any kind. Zero if never connected.
  std::int64_t last_event_ns() const { return last_event_ns_.load(std::memory_order_acquire); }

 private:
  void handle_text_(const std::string& msg);

  Config cfg_;
  core::EventBus& bus_;
  const core::Clock& clock_;
  std::unique_ptr<ix::WebSocket> ws_;
  std::atomic<std::int64_t> last_event_ns_{0};
  std::atomic<bool> running_{false};
};

}  // namespace propr::net
