#include "propr/app/state_machine.h"

namespace propr::app {

const char* to_string(AppState s) {
  switch (s) {
    case AppState::Starting: return "STARTING";
    case AppState::Reconciling: return "RECONCILING";
    case AppState::Live: return "LIVE";
    case AppState::Blind: return "BLIND";
    case AppState::Flattening: return "FLATTENING";
    case AppState::Halted: return "HALTED";
  }
  return "?";
}

StateMachine::StateMachine() : state_(AppState::Starting) {}

bool StateMachine::legal_(AppState from, AppState to) {
  // Any state can fall to HALTED (fatal / manual halt).
  if (to == AppState::Halted) return true;
  // Self-transitions are no-ops at this layer.
  if (from == to) return true;
  switch (from) {
    case AppState::Starting:
      return to == AppState::Reconciling;
    case AppState::Reconciling:
      return to == AppState::Live;
    case AppState::Live:
      return to == AppState::Blind || to == AppState::Flattening;
    case AppState::Blind:
      return to == AppState::Live || to == AppState::Flattening;
    case AppState::Flattening:
      // FLATTENING terminates in HALTED; HALTED handled above.
      return false;
    case AppState::Halted:
      return false;  // terminal
  }
  return false;
}

bool StateMachine::transition(AppState to) {
  AppState from = state_.load(std::memory_order_acquire);
  for (;;) {
    if (!legal_(from, to)) return false;
    if (state_.compare_exchange_weak(from, to, std::memory_order_acq_rel)) {
      std::vector<Listener> copy;
      {
        std::lock_guard<std::mutex> g(listeners_mu_);
        copy = listeners_;
      }
      for (auto& l : copy) l(from, to);
      return true;
    }
  }
}

bool StateMachine::transition_from(AppState from, AppState to) {
  if (!legal_(from, to)) return false;
  AppState expected = from;
  if (!state_.compare_exchange_strong(expected, to, std::memory_order_acq_rel)) {
    return false;
  }
  std::vector<Listener> copy;
  {
    std::lock_guard<std::mutex> g(listeners_mu_);
    copy = listeners_;
  }
  for (auto& l : copy) l(from, to);
  return true;
}

void StateMachine::add_listener(Listener l) {
  std::lock_guard<std::mutex> g(listeners_mu_);
  listeners_.push_back(std::move(l));
}

}  // namespace propr::app
