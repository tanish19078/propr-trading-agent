#pragma once

#include <concurrentqueue.h>

#include <atomic>
#include <variant>

#include "propr/core/events.h"

namespace propr::core {

using Event = std::variant<
    AccountUpdated,
    OrderUpdate,
    PositionUpdate,
    Fill,
    MarketTick,
    KillSwitchTrip,
    ReconcileDivergence,
    DailyReset,
    WsDisconnect,
    WsReconnect>;

// Lock-free MPSC bus. Producers push from any thread; consumer drains on the main loop.
class EventBus {
 public:
  EventBus() = default;
  EventBus(const EventBus&) = delete;
  EventBus& operator=(const EventBus&) = delete;

  void publish(Event ev) {
    queue_.enqueue(std::move(ev));
    pending_.fetch_add(1, std::memory_order_release);
  }

  // Drain up to `max` events; calls `fn(Event&)` for each. Returns count drained.
  template <typename F>
  std::size_t drain(F&& fn, std::size_t max = 256) {
    std::size_t drained = 0;
    Event ev;
    while (drained < max && queue_.try_dequeue(ev)) {
      fn(ev);
      ++drained;
    }
    pending_.fetch_sub(drained, std::memory_order_release);
    return drained;
  }

  std::size_t pending() const { return pending_.load(std::memory_order_acquire); }

 private:
  moodycamel::ConcurrentQueue<Event> queue_;
  std::atomic<std::size_t> pending_{0};
};

}  // namespace propr::core
