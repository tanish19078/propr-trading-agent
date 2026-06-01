#include <gtest/gtest.h>

#include "propr/risk/sizing_policy.h"

using propr::core::usdc;
using propr::risk::SizingPolicy;

TEST(SizingPolicyTest, ZeroEquityZeroRisk) {
  SizingPolicy p;
  EXPECT_EQ(p.max_risk(0, usdc(100), usdc(100)), 0);
}

TEST(SizingPolicyTest, RiskClampedByEquityCap) {
  SizingPolicy p({
      .max_risk_per_trade_bps = 200,        // 2%
      .max_daily_headroom_use_bps = 9999,
      .max_overall_headroom_use_bps = 9999,
  });
  // 2% of 1000 USDC = 20 USDC.
  EXPECT_EQ(p.max_risk(usdc(1000), usdc(1000), usdc(1000)), usdc(20));
}

TEST(SizingPolicyTest, RiskClampedByDailyHeadroom) {
  SizingPolicy p({
      .max_risk_per_trade_bps = 9999,
      .max_daily_headroom_use_bps = 2500,   // 25%
      .max_overall_headroom_use_bps = 9999,
  });
  // 25% of 40 USDC daily = 10 USDC.
  EXPECT_EQ(p.max_risk(usdc(10000), usdc(40), usdc(10000)), usdc(10));
}

TEST(SizingPolicyTest, QtyDerivedFromRiskAndStopGap) {
  SizingPolicy p;
  // risk budget 10 USDC, entry 10000 USDC/BTC, stop 9000 USDC/BTC.
  // gap = 1000 micro-USDC per nano-BTC scaled... use real micro:
  // entry  = 10000 * kMicroPerUnit
  // stop   =  9000 * kMicroPerUnit
  // gap    =  1000 * kMicroPerUnit
  // qty    = (10 USDC * kNanoPerUnit) / 1000 USDC = 0.01 BTC = 10_000_000 nano
  const auto entry = static_cast<propr::core::Price>(10000) * propr::core::kMicroPerUnit;
  const auto stop = static_cast<propr::core::Price>(9000) * propr::core::kMicroPerUnit;
  const auto q = p.max_qty(usdc(10), entry, stop);
  EXPECT_NEAR(static_cast<double>(q), 10'000'000.0, 1000.0);
}
