#pragma once

#include <atomic>
#include <chrono>
#include <thread>

#include "propr/account/account.h"
#include "propr/core/clock.h"
#include "propr/core/event_bus.h"
#include "propr/net/http_client.h"
#include "propr/risk/kill_switch.h"

namespace propr::exec {

// Periodic REST poll comparing live state with our in-memory mirror.
// On mismatch: re-fetch full state, emit ReconcileDivergence, count consecutive
// divergences, trip the kill switch after the threshold.
class Reconciler {
 public:
  Reconciler(net::HttpClient& http,
             account::Account& account,
             core::EventBus& bus,
             risk::KillSwitch& kill,
             const core::Clock& clock,
             std::chrono::seconds interval = std::chrono::seconds{30});
  ~Reconciler();

  void start();
  void stop();

  int consecutive_divergences() const {
    return consecutive_.load(std::memory_order_acquire);
  }

 private:
  void run_();
  bool tick_();  // returns true if reconcile clean

  net::HttpClient& http_;
  account::Account& account_;
  core::EventBus& bus_;
  risk::KillSwitch& kill_;
  const core::Clock& clock_;
  std::chrono::seconds interval_;

  std::atomic<bool> running_{false};
  std::atomic<int> consecutive_{0};
  std::thread thread_;
};

}  // namespace propr::exec
