#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "propr/schemas/v1.h"

using namespace propr::schemas::v1;
using json = nlohmann::json;

template <typename T>
void roundtrip(const T& original) {
  json j = original;
  const std::string encoded = j.dump();
  T decoded = json::parse(encoded).get<T>();
  json j2 = decoded;
  EXPECT_EQ(j, j2) << "encoded: " << encoded;
}

TEST(SchemasRoundtrip, TickV1) {
  TickV1 t{.v = 1,
           .base = "BTC",
           .mark_micro = 60'000'000'000LL,
           .bid_micro = 59'999'000'000LL,
           .ask_micro = 60'001'000'000LL,
           .at_ns = 1234567890LL};
  roundtrip(t);
}

TEST(SchemasRoundtrip, IntentV1WithOptionals) {
  IntentV1 i;
  i.intent_uuid = "U";
  i.strategy_name = "range_mr";
  i.kind = IntentKindV1::OpenShort;
  i.asset_base = "ETH";
  i.quantity_nano = 100'000;
  i.suggested_entry_price_micro = 3'500'000'000LL;
  i.stop_loss_price_micro = 3'550'000'000LL;
  i.take_profit_price_micro = 3'400'000'000LL;
  i.risk_at_stop_micro = 5'000'000LL;
  i.emitted_at_ns = 100;
  roundtrip(i);
}

TEST(SchemasRoundtrip, IntentV1WithoutOptionals) {
  IntentV1 i;
  i.intent_uuid = "U2";
  i.kind = IntentKindV1::Flatten;
  i.asset_base = "BTC";
  roundtrip(i);
}

TEST(SchemasRoundtrip, OrderCommandV1Full) {
  OrderCommandV1 c;
  c.command_uuid = "C";
  c.intent_uuid = "I";
  c.order_group_id = "G";
  c.entry_intent_id = "E";
  c.stop_intent_id = "S";
  c.tp_intent_id = "T";
  c.asset_base = "BTC";
  c.entry_side = "buy";
  c.position_side = "long";
  c.quantity_nano = 1000;
  c.entry_limit_price_micro = 60'000'000'000LL;
  c.stop_trigger_price_micro = 59'500'000'000LL;
  c.tp_price_micro = 60'300'000'000LL;
  c.expected_position_delta_micro = 60'000'000LL;
  c.max_slippage_bps = 25;
  c.issued_at_ns = 1;
  c.expires_at_ns = 2;
  c.hmac_hex = "deadbeef";
  roundtrip(c);
}

TEST(SchemasRoundtrip, RiskDecisionWithEmbeddedCommand) {
  RiskDecisionV1 d;
  d.intent_uuid = "I";
  d.outcome = RiskOutcomeV1::Resize;
  d.reason = RiskReasonV1::Approved;
  d.decided_at_ns = 5;
  OrderCommandV1 c;
  c.command_uuid = "C";
  c.asset_base = "ETH";
  c.entry_side = "sell";
  c.position_side = "short";
  c.hmac_hex = "ff";
  d.command = c;
  roundtrip(d);
}

TEST(SchemasRoundtrip, ExecutionReportV1) {
  ExecutionReportV1 r;
  r.command_uuid = "C";
  r.intent_uuid = "I";
  r.status = ExecutionStatusV1::PartiallyFilled;
  r.filled_quantity_nano = 500;
  r.average_fill_price_micro = 60'000'000'000LL;
  r.fee_micro = 4500;
  r.at_ns = 9;
  roundtrip(r);
}

TEST(SchemasRoundtrip, KillSwitchTripV1) {
  KillSwitchTripV1 k;
  k.reason = KillSwitchReasonV1::FloatingLossExceeded;
  k.detail = "70% of overall DD consumed";
  k.equity_at_trip_micro = 9'400'000'000LL;
  k.headroom_at_trip_micro = 60'000'000LL;
  k.at_ns = 1234;
  roundtrip(k);
}
