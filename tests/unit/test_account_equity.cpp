#include <gtest/gtest.h>

#include "propr/account/account.h"

using propr::account::Account;
using propr::core::usdc;

TEST(AccountTest, EquityFormulaIsBalancePlusUpnlPlusIsolatedMargin) {
  Account a;
  a.apply_account_update(usdc(1000), usdc(50), usdc(200), usdc(0), usdc(1250));
  EXPECT_EQ(a.equity(), usdc(1000) + usdc(50) + usdc(200));
}

TEST(AccountTest, HighWaterMarkIsMonotonic) {
  Account a;
  a.apply_account_update(usdc(1000), 0, 0, 0, usdc(1200));
  a.apply_account_update(usdc(900), 0, 0, 0, usdc(1100));   // lower hwm reported
  EXPECT_EQ(a.high_water_mark(), usdc(1200));               // we don't go backwards
  a.apply_account_update(usdc(1300), 0, 0, 0, usdc(1300));
  EXPECT_EQ(a.high_water_mark(), usdc(1300));
}

TEST(AccountTest, HasMarginAccountsForCrossAndIsolated) {
  Account a;
  a.apply_account_update(usdc(1000), 0, usdc(200), usdc(300), usdc(1000));
  // Free = balance - cross - isolated = 1000 - 300 - 200 = 500.
  EXPECT_TRUE(a.has_margin_for(usdc(500)));
  EXPECT_FALSE(a.has_margin_for(usdc(501)));
}
