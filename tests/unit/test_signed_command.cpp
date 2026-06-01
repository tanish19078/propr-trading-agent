#include <gtest/gtest.h>

#include <random>

#include "propr/schemas/sign.h"
#include "propr/schemas/v1.h"

using propr::schemas::v1::OrderCommandV1;

namespace {
OrderCommandV1 sample(std::uint64_t seed) {
  std::mt19937_64 rng(seed);
  OrderCommandV1 c;
  c.command_uuid = "CMD" + std::to_string(rng());
  c.intent_uuid = "INT" + std::to_string(rng());
  c.order_group_id = "GRP" + std::to_string(rng());
  c.entry_intent_id = "EID" + std::to_string(rng());
  c.stop_intent_id = std::string("SID") + std::to_string(rng());
  c.tp_intent_id = std::string("TID") + std::to_string(rng());
  c.asset_base = (rng() & 1u) ? "BTC" : "ETH";
  c.entry_side = (rng() & 1u) ? "buy" : "sell";
  c.position_side = c.entry_side == "buy" ? "long" : "short";
  c.quantity_nano = static_cast<propr::core::Qty>(rng() % 10'000'000ULL + 1);
  c.entry_limit_price_micro = static_cast<propr::core::Price>(rng() % 100'000'000ULL + 1);
  c.stop_trigger_price_micro = static_cast<propr::core::Price>(rng() % 100'000'000ULL + 1);
  c.tp_price_micro = static_cast<propr::core::Price>(rng() % 100'000'000ULL + 1);
  c.expected_position_delta_micro =
      static_cast<propr::core::Money>(rng() % 10'000'000ULL + 1);
  c.max_slippage_bps = static_cast<int>(rng() % 100);
  c.issued_at_ns = static_cast<propr::core::Nanos>(rng());
  c.expires_at_ns = c.issued_at_ns + 5'000'000'000LL;
  return c;
}
}  // namespace

TEST(SignedCommandTest, SignThenVerifyRoundtrips) {
  for (std::uint64_t s = 1; s < 200; ++s) {
    auto c = propr::schemas::sign(sample(s), "secret");
    EXPECT_TRUE(propr::schemas::verify(c, "secret")) << "seed=" << s;
  }
}

TEST(SignedCommandTest, WrongSecretFails) {
  auto c = propr::schemas::sign(sample(1), "secret-a");
  EXPECT_FALSE(propr::schemas::verify(c, "secret-b"));
}

TEST(SignedCommandTest, AnyMutationBreaksHmac) {
  auto c = propr::schemas::sign(sample(7), "shh");
  // Quantity tamper.
  auto t1 = c;
  t1.quantity_nano += 1;
  EXPECT_FALSE(propr::schemas::verify(t1, "shh"));

  // Side tamper.
  auto t2 = c;
  t2.entry_side = (c.entry_side == "buy") ? "sell" : "buy";
  EXPECT_FALSE(propr::schemas::verify(t2, "shh"));

  // Expiry tamper.
  auto t3 = c;
  t3.expires_at_ns += 1;
  EXPECT_FALSE(propr::schemas::verify(t3, "shh"));

  // Slippage tamper.
  auto t4 = c;
  t4.max_slippage_bps += 1;
  EXPECT_FALSE(propr::schemas::verify(t4, "shh"));
}

TEST(SignedCommandTest, EmptyHmacAlwaysFails) {
  auto c = sample(42);
  c.hmac_hex.clear();
  EXPECT_FALSE(propr::schemas::verify(c, "x"));
}
