#include <gtest/gtest.h>

#include "propr/app/state_machine.h"

using propr::app::AppState;
using propr::app::StateMachine;

TEST(StateMachineTest, BeginsAtStarting) {
  StateMachine sm;
  EXPECT_EQ(sm.state(), AppState::Starting);
  EXPECT_FALSE(sm.allows_new_entries());
  EXPECT_FALSE(sm.allows_flatten());
}

TEST(StateMachineTest, LegalForwardPath) {
  StateMachine sm;
  EXPECT_TRUE(sm.transition(AppState::Reconciling));
  EXPECT_TRUE(sm.transition(AppState::Live));
  EXPECT_TRUE(sm.allows_new_entries());
  EXPECT_TRUE(sm.transition(AppState::Blind));
  EXPECT_FALSE(sm.allows_new_entries());
  EXPECT_TRUE(sm.allows_flatten());
  EXPECT_TRUE(sm.transition(AppState::Live));  // recover
  EXPECT_TRUE(sm.transition(AppState::Flattening));
  EXPECT_TRUE(sm.transition(AppState::Halted));
}

TEST(StateMachineTest, IllegalSkipsAreRejected) {
  StateMachine sm;
  EXPECT_FALSE(sm.transition(AppState::Live));    // skip Reconciling
  EXPECT_FALSE(sm.transition(AppState::Blind));
  EXPECT_FALSE(sm.transition(AppState::Flattening));
}

TEST(StateMachineTest, AnyStateCanFallToHalted) {
  for (auto s : {AppState::Starting, AppState::Reconciling, AppState::Live,
                 AppState::Blind, AppState::Flattening}) {
    StateMachine sm;
    if (s != AppState::Starting) sm.transition(AppState::Reconciling);
    if (s == AppState::Live || s == AppState::Blind || s == AppState::Flattening)
      sm.transition(AppState::Live);
    if (s == AppState::Blind) sm.transition(AppState::Blind);
    if (s == AppState::Flattening) sm.transition(AppState::Flattening);
    EXPECT_TRUE(sm.transition(AppState::Halted));
    EXPECT_EQ(sm.state(), AppState::Halted);
  }
}

TEST(StateMachineTest, HaltedIsTerminal) {
  StateMachine sm;
  sm.transition(AppState::Reconciling);
  sm.transition(AppState::Halted);
  EXPECT_FALSE(sm.transition(AppState::Live));
  EXPECT_FALSE(sm.transition(AppState::Reconciling));
}

TEST(StateMachineTest, ListenerFiresOnTransition) {
  StateMachine sm;
  int calls = 0;
  AppState last_from = AppState::Halted, last_to = AppState::Halted;
  sm.add_listener([&](AppState f, AppState t) {
    ++calls;
    last_from = f;
    last_to = t;
  });
  sm.transition(AppState::Reconciling);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(last_from, AppState::Starting);
  EXPECT_EQ(last_to, AppState::Reconciling);
}
