// Seeded-random invariants. These are not exhaustive proofs, but they exercise
// thousands of input combinations against the contracts that must always hold.

#include <gtest/gtest.h>

#include <random>

#include "fakes/fake_clock.h"
#include "propr/account/account.h"
#include "propr/app/state_machine.h"
#include "propr/core/ulid.h"
#include "propr/risk/sizing_policy.h"

using propr::account::Account;
using propr::core::Money;
using propr::core::Ulid;
using propr::core::usdc;
using propr::risk::SizingPolicy;

TEST(Invariants, MaxRiskNeverExceedsAnyCap) {
  std::mt19937_64 rng(0xC0FFEE);
  SizingPolicy::Caps c{.max_risk_per_trade_bps = 200,
                       .max_daily_headroom_use_bps = 2500,
                       .max_overall_headroom_use_bps = 1000};
  SizingPolicy p(c);
  for (int i = 0; i < 5000; ++i) {
    const Money eq = static_cast<Money>(rng() % usdc(100000) + 1);
    const Money daily = static_cast<Money>(rng() % eq + 1);
    const Money overall = static_cast<Money>(rng() % eq + 1);
    const Money r = p.max_risk(eq, daily, overall);
    ASSERT_LE(r, (eq * c.max_risk_per_trade_bps) / 10000) << "eq cap";
    ASSERT_LE(r, (daily * c.max_daily_headroom_use_bps) / 10000) << "daily cap";
    ASSERT_LE(r, (overall * c.max_overall_headroom_use_bps) / 10000) << "overall cap";
    ASSERT_GE(r, 0);
  }
}

TEST(Invariants, AnyZeroInputClampsToZero) {
  std::mt19937_64 rng(7);
  SizingPolicy p;
  for (int i = 0; i < 1000; ++i) {
    const Money eq = static_cast<Money>(rng() % usdc(1000));
    const Money daily = static_cast<Money>(rng() % usdc(1000));
    const Money overall = static_cast<Money>(rng() % usdc(1000));
    if (eq == 0) ASSERT_EQ(p.max_risk(eq, daily, overall), 0);
    if (daily == 0) ASSERT_EQ(p.max_risk(eq, daily, overall), 0);
    if (overall == 0) ASSERT_EQ(p.max_risk(eq, daily, overall), 0);
  }
}

TEST(Invariants, EquityFormulaIsExactly_Balance_plus_Upnl_plus_IsolatedMargin) {
  std::mt19937_64 rng(0xBEEF);
  Account a;
  for (int i = 0; i < 1000; ++i) {
    const Money balance = static_cast<Money>(rng() % usdc(50000));
    const Money upnl =
        static_cast<Money>(static_cast<int64_t>(rng() % usdc(2000)) - usdc(1000));
    const Money iso = static_cast<Money>(rng() % usdc(5000));
    const Money xpm = static_cast<Money>(rng() % usdc(5000));
    const Money hwm = balance + upnl + iso + usdc(100);
    a.apply_account_update(balance, upnl, iso, xpm, hwm);
    ASSERT_EQ(a.equity(), balance + upnl + iso);
  }
}

TEST(Invariants, UlidIsAlwaysLexSorted_UnderThreadContention) {
  // 8 threads, 1000 ulids each. Every produced ULID must be greater than any ULID
  // generated previously in the same thread (single-thread monotonicity) AND any
  // pair of ULIDs produced in distinct threads in the same millisecond must compare
  // lexicographically without ties.
  Ulid u;
  constexpr int kPerThread = 1000;
  constexpr int kThreads = 8;
  std::vector<std::vector<std::string>> per_thread(kThreads);
  std::vector<std::thread> ts;
  for (int t = 0; t < kThreads; ++t) {
    ts.emplace_back([&, t]() {
      auto& v = per_thread[t];
      v.reserve(kPerThread);
      for (int i = 0; i < kPerThread; ++i) v.push_back(u.next());
      for (int i = 1; i < kPerThread; ++i) {
        ASSERT_LT(v[i - 1], v[i]);
      }
    });
  }
  for (auto& th : ts) th.join();
  // Cross-thread uniqueness.
  std::set<std::string> all;
  for (const auto& v : per_thread)
    for (const auto& s : v) ASSERT_TRUE(all.insert(s).second);
}

TEST(Invariants, BlindAndHaltedNeverAllowNewEntries) {
  for (auto path : std::vector<std::vector<propr::app::AppState>>{
           {propr::app::AppState::Reconciling, propr::app::AppState::Live,
            propr::app::AppState::Blind},
           {propr::app::AppState::Reconciling, propr::app::AppState::Halted},
           {propr::app::AppState::Reconciling, propr::app::AppState::Live,
            propr::app::AppState::Flattening},
       }) {
    propr::app::StateMachine sm;
    for (auto s : path) ASSERT_TRUE(sm.transition(s));
    EXPECT_FALSE(sm.allows_new_entries())
        << "state=" << propr::app::to_string(sm.state());
  }
}
