#include <gtest/gtest.h>

#include "propr/core/decimal.h"

using propr::core::parse_money_micro;
using propr::core::parse_qty_nano;
using propr::core::parse_price_micro;

TEST(Decimal, IntegerStrings) {
  EXPECT_EQ(parse_money_micro("0").value(), 0);
  EXPECT_EQ(parse_money_micro("1").value(), 1'000'000);
  EXPECT_EQ(parse_money_micro("5000").value(), 5'000'000'000);
  EXPECT_EQ(parse_money_micro("5000.0").value(), 5'000'000'000);
}

TEST(Decimal, FractionalStrings) {
  EXPECT_EQ(parse_money_micro("0.5").value(), 500'000);
  EXPECT_EQ(parse_money_micro("0.123456").value(), 123'456);
  EXPECT_EQ(parse_money_micro("12.5").value(), 12'500'000);
}

TEST(Decimal, NegativeAndPositive) {
  EXPECT_EQ(parse_money_micro("-12.5").value(), -12'500'000);
  EXPECT_EQ(parse_money_micro("+3.14").value(), 3'140'000);
}

TEST(Decimal, WhitespaceIsTrimmed) {
  EXPECT_EQ(parse_money_micro("  1.0  ").value(), 1'000'000);
}

TEST(Decimal, RejectsScientificAndJunk) {
  EXPECT_FALSE(parse_money_micro("5e3").has_value());
  EXPECT_FALSE(parse_money_micro("abc").has_value());
  EXPECT_FALSE(parse_money_micro("1.2.3").has_value());
  EXPECT_FALSE(parse_money_micro("").has_value());
  EXPECT_FALSE(parse_money_micro("1.5x").has_value());
  EXPECT_FALSE(parse_money_micro("-").has_value());
}

TEST(Decimal, RejectsExcessFractionalDigitsForScale) {
  // micro = 6 digits. 7+ digits silently truncates if we don't reject.
  EXPECT_FALSE(parse_money_micro("0.0000001").has_value());
  // But trailing zeros beyond scale are fine.
  EXPECT_EQ(parse_money_micro("0.1000000").value(), 100'000);
}

TEST(Decimal, NanoScaleAcceptsNineDigits) {
  EXPECT_EQ(parse_qty_nano("0.000000001").value(), 1);
  EXPECT_EQ(parse_qty_nano("0.5").value(), 500'000'000);
  EXPECT_FALSE(parse_qty_nano("0.0000000001").has_value());
}

TEST(Decimal, PriceAlias) {
  EXPECT_EQ(parse_price_micro("60000.5").value(), 60'000'500'000LL);
}
