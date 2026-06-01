#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace propr::app {

// Explicit lifecycle state machine. Every code path that decides whether to trade,
// flatten, or halt MUST consult this rather than guessing from booleans.
//
// Legal transitions:
//   STARTING    -> RECONCILING
//   RECONCILING -> LIVE
//   RECONCILING -> HALTED       (preflight failed)
//   LIVE        -> BLIND        (WS gap, reconcile divergence)
//   LIVE        -> FLATTENING   (kill switch tripped)
//   BLIND       -> LIVE         (WS up + N clean reconciles)
//   BLIND       -> FLATTENING   (kill switch tripped while blind)
//   BLIND       -> HALTED       (manual)
//   FLATTENING  -> HALTED       (flatten completed)
//   any         -> HALTED       (manual / fatal)
enum class AppState {
  Starting,
  Reconciling,
  Live,
  Blind,
  Flattening,
  Halted,
};

const char* to_string(AppState s);

class StateMachine {
 public:
  using Listener = std::function<void(AppState from, AppState to)>;

  StateMachine();

  AppState state() const { return state_.load(std::memory_order_acquire); }

  // Returns true if the transition is legal and was applied.
  bool transition(AppState to);

  // Same, but requires `from` to match the current state (compare-and-swap semantics).
  bool transition_from(AppState from, AppState to);

  // Permissions. Single source of truth for "can I do X right now?".
  bool allows_new_entries() const { return state() == AppState::Live; }
  bool allows_flatten() const {
    const auto s = state();
    return s == AppState::Live || s == AppState::Blind || s == AppState::Flattening;
  }
  bool allows_strategy_callbacks() const {
    const auto s = state();
    return s == AppState::Live || s == AppState::Blind;
  }

  void add_listener(Listener l);

 private:
  static bool legal_(AppState from, AppState to);

  std::atomic<AppState> state_;
  std::mutex listeners_mu_;
  std::vector<Listener> listeners_;
};

}  // namespace propr::app
