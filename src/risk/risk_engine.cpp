#include "propr/risk/risk_engine.h"

#include <algorithm>

#include "propr/schemas/sign.h"

namespace propr::risk {

namespace {
using propr::schemas::v1::IntentKindV1;
using propr::schemas::v1::IntentV1;
using propr::schemas::v1::OrderCommandV1;
using propr::schemas::v1::RiskDecisionV1;
using propr::schemas::v1::RiskOutcomeV1;
using propr::schemas::v1::RiskReasonV1;

bool is_entry(IntentKindV1 k) {
  return k == IntentKindV1::OpenLong || k == IntentKindV1::OpenShort;
}
bool is_flatten_class(IntentKindV1 k) {
  return k == IntentKindV1::CancelAll || k == IntentKindV1::Flatten;
}
}  // namespace

RiskEngine::RiskEngine(const account::Account& account,
                       const account::ChallengeRules& rules,
                       const app::StateMachine& sm,
                       KillSwitch& kill,
                       RateLimiter& rate,
                       LeverageCap leverage,
                       SizingPolicy sizing,
                       core::Ulid& ulid,
                       const core::Clock& clock,
                       std::string_view hmac_secret,
                       Config cfg)
    : account_(account),
      rules_(rules),
      sm_(sm),
      kill_(kill),
      rate_(rate),
      leverage_(leverage),
      sizing_(sizing),
      ulid_(ulid),
      clock_(clock),
      hmac_secret_(hmac_secret),
      cfg_(cfg) {}

OrderCommandV1 RiskEngine::build_signed_command_(const IntentV1& intent,
                                                 core::Qty final_qty) {
  OrderCommandV1 c;
  c.command_uuid = ulid_.next();
  c.intent_uuid = intent.intent_uuid;
  c.order_group_id = ulid_.next();
  c.entry_intent_id = ulid_.next();
  if (intent.stop_loss_price_micro) c.stop_intent_id = ulid_.next();
  if (intent.take_profit_price_micro) c.tp_intent_id = ulid_.next();
  c.asset_base = intent.asset_base;
  const bool is_long = intent.kind == IntentKindV1::OpenLong;
  c.entry_side = is_long ? "buy" : "sell";
  c.position_side = is_long ? "long" : "short";
  c.quantity_nano = final_qty;
  // Marketable-limit: price the entry with a slippage buffer baked in.
  const auto factor_micro = static_cast<__int128>(cfg_.max_slippage_bps);
  const __int128 buf = (static_cast<__int128>(intent.suggested_entry_price_micro) *
                        factor_micro) /
                       10000;
  c.entry_limit_price_micro = is_long
                                  ? static_cast<core::Price>(
                                        intent.suggested_entry_price_micro + buf)
                                  : static_cast<core::Price>(
                                        intent.suggested_entry_price_micro - buf);
  c.stop_trigger_price_micro = intent.stop_loss_price_micro;
  c.tp_price_micro = intent.take_profit_price_micro;
  c.expected_position_delta_micro =
      core::notional(c.entry_limit_price_micro, c.quantity_nano);
  c.max_slippage_bps = cfg_.max_slippage_bps;
  c.issued_at_ns = clock_.now_ns();
  c.expires_at_ns =
      c.issued_at_ns + static_cast<core::Nanos>(cfg_.command_ttl_ms) * 1'000'000LL;
  return schemas::sign(c, hmac_secret_);
}

RiskDecisionV1 RiskEngine::evaluate(const IntentV1& intent) {
  RiskDecisionV1 d;
  d.intent_uuid = intent.intent_uuid;
  d.decided_at_ns = clock_.now_ns();
  d.outcome = RiskOutcomeV1::Reject;

  // 1. State machine: only LIVE allows new entries. Flatten/CancelAll allowed in
  //    LIVE, BLIND, and FLATTENING.
  const bool flatten_class = is_flatten_class(intent.kind);
  if (flatten_class) {
    if (!sm_.allows_flatten()) {
      d.reason = RiskReasonV1::StateNotLive;
      d.detail = std::string("state=") + app::to_string(sm_.state());
      return d;
    }
  } else {
    if (!sm_.allows_new_entries()) {
      d.reason = RiskReasonV1::StateNotLive;
      d.detail = std::string("state=") + app::to_string(sm_.state());
      return d;
    }
  }

  // 2. Kill switch.
  if (kill_.armed() && !flatten_class) {
    d.reason = RiskReasonV1::KillSwitchArmed;
    d.detail = kill_.reason();
    return d;
  }

  // 3. Rate budget.
  const auto rate_class =
      flatten_class ? RateLimiter::Class::Reserved : RateLimiter::Class::Normal;
  if (!rate_.try_take(rate_class)) {
    d.reason = RiskReasonV1::RateLimited;
    return d;
  }

  // Non-entry intents bypass sizing.
  if (!is_entry(intent.kind)) {
    d.outcome = RiskOutcomeV1::Approve;
    d.reason = RiskReasonV1::Approved;
    return d;
  }

  // 4. Validate geometry.
  if (intent.quantity_nano <= 0 || intent.suggested_entry_price_micro <= 0 ||
      !intent.stop_loss_price_micro.has_value() || *intent.stop_loss_price_micro <= 0) {
    d.reason = RiskReasonV1::InvalidIntent;
    return d;
  }

  // 5. Sizing.
  const core::Money eq = account_.equity();
  const core::Money hwm = account_.high_water_mark();
  const core::Money overall_headroom =
      std::max<core::Money>(eq - rules_.dd_floor_from(hwm), 0);
  const core::Money daily_headroom =
      daily_snapshot_ > 0
          ? std::max<core::Money>(eq - rules_.daily_floor_from(daily_snapshot_), 0)
          : overall_headroom;
  const core::Money risk_budget = sizing_.max_risk(eq, daily_headroom, overall_headroom);
  if (risk_budget <= 0) {
    d.reason = RiskReasonV1::SizeClampedToZero;
    return d;
  }
  const core::Qty max_qty = sizing_.max_qty(risk_budget,
                                            intent.suggested_entry_price_micro,
                                            *intent.stop_loss_price_micro);
  if (max_qty <= 0) {
    d.reason = RiskReasonV1::SizeClampedToZero;
    return d;
  }
  const core::Qty final_qty = std::min(intent.quantity_nano, max_qty);

  // 6. Leverage cap (notional <= cap * equity).
  const core::Asset asset{intent.asset_base};
  const core::Money notional =
      core::notional(intent.suggested_entry_price_micro, final_qty);
  if (!leverage_.permits(asset, notional, eq)) {
    d.reason = RiskReasonV1::LeverageExceeded;
    return d;
  }

  // 7. Margin.
  const core::Money required_margin = notional / std::max(leverage_.max_for(asset), 1);
  if (!account_.has_margin_for(required_margin)) {
    d.reason = RiskReasonV1::MarginInsufficient;
    return d;
  }

  d.outcome = (final_qty < intent.quantity_nano) ? RiskOutcomeV1::Resize
                                                  : RiskOutcomeV1::Approve;
  d.reason = RiskReasonV1::Approved;
  d.command = build_signed_command_(intent, final_qty);
  return d;
}

}  // namespace propr::risk
