// schemas/v1.h - the SINGLE canonical wire/journal format.
//
// Every cross-module message carries a `v` field. Bump kVersion and add a new
// header (v2.h) for any breaking change. We never "agree by vibes" - if a struct
// is not defined here, it does not cross a module boundary.
//
// Encoding: nlohmann::json. Enums serialize as strings so logs are readable.
// std::optional fields are serialized only when present.
//
// All money fields are int64 micro-USDC. All prices are int64 micro-USDC/base.
// All quantities are int64 nano-base. All timestamps are int64 ns since epoch UTC.

#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

#include "propr/core/types.h"

namespace propr::schemas::v1 {

constexpr int kVersion = 1;

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

enum class IntentKindV1 { OpenLong, OpenShort, ClosePosition, CancelAll, Flatten };

enum class RiskOutcomeV1 { Approve, Resize, Reject };

enum class RiskReasonV1 {
  Approved,
  StateNotLive,
  KillSwitchArmed,
  RateLimited,
  LeverageExceeded,
  MarginInsufficient,
  SizeClampedToZero,
  InvalidIntent,
};

enum class ExecutionStatusV1 {
  Accepted,
  RejectedExpired,
  RejectedBadHmac,
  RejectedNotLive,
  Filled,
  PartiallyFilled,
  Cancelled,
  NetworkError,
};

enum class PositionStatusV1 {
  Opened,
  Updated,
  Closed,
  Liquidated,
  TakeProfitHit,
  StopLossHit,
};

enum class KillSwitchReasonV1 {
  FloatingLossExceeded,
  DailyLossExceeded,
  WsDisconnected,
  ReconcileDivergence,
  ManualHalt,
};

enum class ReconcileFieldV1 { Balance, Positions, OpenOrders };

NLOHMANN_JSON_SERIALIZE_ENUM(IntentKindV1,
                              {{IntentKindV1::OpenLong, "open_long"},
                               {IntentKindV1::OpenShort, "open_short"},
                               {IntentKindV1::ClosePosition, "close_position"},
                               {IntentKindV1::CancelAll, "cancel_all"},
                               {IntentKindV1::Flatten, "flatten"}})

NLOHMANN_JSON_SERIALIZE_ENUM(RiskOutcomeV1, {{RiskOutcomeV1::Approve, "approve"},
                                              {RiskOutcomeV1::Resize, "resize"},
                                              {RiskOutcomeV1::Reject, "reject"}})

NLOHMANN_JSON_SERIALIZE_ENUM(RiskReasonV1,
                              {{RiskReasonV1::Approved, "approved"},
                               {RiskReasonV1::StateNotLive, "state_not_live"},
                               {RiskReasonV1::KillSwitchArmed, "kill_switch_armed"},
                               {RiskReasonV1::RateLimited, "rate_limited"},
                               {RiskReasonV1::LeverageExceeded, "leverage_exceeded"},
                               {RiskReasonV1::MarginInsufficient, "margin_insufficient"},
                               {RiskReasonV1::SizeClampedToZero, "size_clamped_to_zero"},
                               {RiskReasonV1::InvalidIntent, "invalid_intent"}})

NLOHMANN_JSON_SERIALIZE_ENUM(ExecutionStatusV1,
                              {{ExecutionStatusV1::Accepted, "accepted"},
                               {ExecutionStatusV1::RejectedExpired, "rejected_expired"},
                               {ExecutionStatusV1::RejectedBadHmac, "rejected_bad_hmac"},
                               {ExecutionStatusV1::RejectedNotLive, "rejected_not_live"},
                               {ExecutionStatusV1::Filled, "filled"},
                               {ExecutionStatusV1::PartiallyFilled, "partially_filled"},
                               {ExecutionStatusV1::Cancelled, "cancelled"},
                               {ExecutionStatusV1::NetworkError, "network_error"}})

NLOHMANN_JSON_SERIALIZE_ENUM(PositionStatusV1,
                              {{PositionStatusV1::Opened, "opened"},
                               {PositionStatusV1::Updated, "updated"},
                               {PositionStatusV1::Closed, "closed"},
                               {PositionStatusV1::Liquidated, "liquidated"},
                               {PositionStatusV1::TakeProfitHit, "tp_hit"},
                               {PositionStatusV1::StopLossHit, "sl_hit"}})

NLOHMANN_JSON_SERIALIZE_ENUM(KillSwitchReasonV1,
                              {{KillSwitchReasonV1::FloatingLossExceeded, "floating_loss"},
                               {KillSwitchReasonV1::DailyLossExceeded, "daily_loss"},
                               {KillSwitchReasonV1::WsDisconnected, "ws_disconnected"},
                               {KillSwitchReasonV1::ReconcileDivergence, "reconcile_divergence"},
                               {KillSwitchReasonV1::ManualHalt, "manual_halt"}})

NLOHMANN_JSON_SERIALIZE_ENUM(ReconcileFieldV1, {{ReconcileFieldV1::Balance, "balance"},
                                                 {ReconcileFieldV1::Positions, "positions"},
                                                 {ReconcileFieldV1::OpenOrders, "open_orders"}})

// ---------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------

struct TickV1 {
  int v{kVersion};
  std::string base;
  core::Price mark_micro{0};
  core::Price bid_micro{0};
  core::Price ask_micro{0};
  core::Nanos at_ns{0};
};

struct AccountSnapshotV1 {
  int v{kVersion};
  std::string account_id;
  std::string source;  // "ws" | "rest_reconcile" | "boot" | "sim"
  core::Money balance_micro{0};
  core::Money total_unrealized_pnl_micro{0};
  core::Money isolated_position_margin_micro{0};
  core::Money cross_position_margin_micro{0};
  core::Money high_water_mark_micro{0};
  bool has_balance{false};
  bool has_upnl{false};
  bool has_iso{false};
  bool has_xpm{false};
  bool has_hwm{false};
  core::Nanos at_ns{0};
};

struct IntentV1 {
  int v{kVersion};
  std::string intent_uuid;
  std::string strategy_name;
  IntentKindV1 kind{IntentKindV1::OpenLong};
  std::string asset_base;
  core::Qty quantity_nano{0};
  core::Price suggested_entry_price_micro{0};
  std::optional<core::Price> stop_loss_price_micro;
  std::optional<core::Price> take_profit_price_micro;
  core::Money risk_at_stop_micro{0};
  core::Nanos emitted_at_ns{0};
};

struct OrderCommandV1 {
  int v{kVersion};
  std::string command_uuid;
  std::string intent_uuid;
  std::string order_group_id;
  std::string entry_intent_id;
  std::optional<std::string> stop_intent_id;
  std::optional<std::string> tp_intent_id;
  std::string asset_base;
  std::string entry_side;     // "buy" | "sell"
  std::string position_side;  // "long" | "short"
  core::Qty quantity_nano{0};
  core::Price entry_limit_price_micro{0};
  std::optional<core::Price> stop_trigger_price_micro;
  std::optional<core::Price> tp_price_micro;
  core::Money expected_position_delta_micro{0};
  int max_slippage_bps{0};
  core::Nanos issued_at_ns{0};
  core::Nanos expires_at_ns{0};
  std::string hmac_hex;
};

struct RiskDecisionV1 {
  int v{kVersion};
  std::string intent_uuid;
  RiskOutcomeV1 outcome{RiskOutcomeV1::Reject};
  RiskReasonV1 reason{RiskReasonV1::InvalidIntent};
  std::string detail;
  std::optional<OrderCommandV1> command;
  core::Nanos decided_at_ns{0};
};

struct ExecutionReportV1 {
  int v{kVersion};
  std::string command_uuid;
  std::string intent_uuid;
  ExecutionStatusV1 status{ExecutionStatusV1::NetworkError};
  std::string detail;
  core::Qty filled_quantity_nano{0};
  core::Price average_fill_price_micro{0};
  core::Money fee_micro{0};
  core::Nanos at_ns{0};
};

struct FillV1 {
  int v{kVersion};
  std::string trade_id;
  std::string order_id;
  std::string position_id;
  std::string asset_base;
  std::string side;
  core::Qty quantity_nano{0};
  core::Price price_micro{0};
  core::Price mark_price_at_order_micro{0};
  core::Money fee_micro{0};
  core::Money slippage_micro{0};
  core::Nanos at_ns{0};
};

struct PositionUpdateV1 {
  int v{kVersion};
  std::string position_id;
  std::string asset_base;
  std::string position_side;
  core::Qty quantity_nano{0};
  core::Price entry_price_micro{0};
  core::Price mark_price_micro{0};
  core::Money unrealized_pnl_micro{0};
  core::Money realized_pnl_micro{0};
  core::Money margin_used_micro{0};
  PositionStatusV1 status{PositionStatusV1::Opened};
  core::Nanos at_ns{0};
};

struct KillSwitchTripV1 {
  int v{kVersion};
  KillSwitchReasonV1 reason{KillSwitchReasonV1::ManualHalt};
  std::string detail;
  core::Money equity_at_trip_micro{0};
  core::Money headroom_at_trip_micro{0};
  core::Nanos at_ns{0};
};

struct ReconcileDivergenceV1 {
  int v{kVersion};
  ReconcileFieldV1 field{ReconcileFieldV1::Balance};
  std::string detail;
  core::Nanos at_ns{0};
};

struct DailyResetV1 {
  int v{kVersion};
  core::Money equity_snapshot_micro{0};
  core::Nanos boundary_ns{0};
};

struct WsStatusV1 {
  int v{kVersion};
  std::string ws_name;
  bool connected{false};
  core::Nanos at_ns{0};
};

// ---------------------------------------------------------------------------
// nlohmann::json round-trip
// ---------------------------------------------------------------------------
// We do not use NLOHMANN_DEFINE_TYPE_INTRUSIVE because of std::optional fields.
// Hand-written to_json/from_json keep optionals out of the JSON when absent.

#define PROPR_J_GET(j, key, target, def)        \
  do {                                          \
    if ((j).contains(key) && !(j)[key].is_null()) (j).at(key).get_to(target); else (target) = def; \
  } while (0)

#define PROPR_J_OPT(j, key, target)                                          \
  do {                                                                       \
    if ((j).contains(key) && !(j)[key].is_null())                            \
      target = (j)[key].get<std::decay_t<decltype(*target)>>();              \
    else                                                                     \
      target.reset();                                                        \
  } while (0)

inline void to_json(nlohmann::json& j, const TickV1& t) {
  j = {{"v", t.v},        {"base", t.base},
       {"mark", t.mark_micro}, {"bid", t.bid_micro},
       {"ask", t.ask_micro}, {"at_ns", t.at_ns}};
}
inline void from_json(const nlohmann::json& j, TickV1& t) {
  PROPR_J_GET(j, "v", t.v, kVersion);
  PROPR_J_GET(j, "base", t.base, std::string{});
  PROPR_J_GET(j, "mark", t.mark_micro, core::Price{0});
  PROPR_J_GET(j, "bid", t.bid_micro, core::Price{0});
  PROPR_J_GET(j, "ask", t.ask_micro, core::Price{0});
  PROPR_J_GET(j, "at_ns", t.at_ns, core::Nanos{0});
}

inline void to_json(nlohmann::json& j, const AccountSnapshotV1& a) {
  j = {{"v", a.v},
       {"account_id", a.account_id},
       {"source", a.source},
       {"balance", a.balance_micro},
       {"upnl", a.total_unrealized_pnl_micro},
       {"iso_margin", a.isolated_position_margin_micro},
       {"cross_margin", a.cross_position_margin_micro},
       {"hwm", a.high_water_mark_micro},
       {"has_balance", a.has_balance},
       {"has_upnl", a.has_upnl},
       {"has_iso", a.has_iso},
       {"has_xpm", a.has_xpm},
       {"has_hwm", a.has_hwm},
       {"at_ns", a.at_ns}};
}
inline void from_json(const nlohmann::json& j, AccountSnapshotV1& a) {
  PROPR_J_GET(j, "v", a.v, kVersion);
  PROPR_J_GET(j, "account_id", a.account_id, std::string{});
  PROPR_J_GET(j, "source", a.source, std::string{});
  PROPR_J_GET(j, "balance", a.balance_micro, core::Money{0});
  PROPR_J_GET(j, "upnl", a.total_unrealized_pnl_micro, core::Money{0});
  PROPR_J_GET(j, "iso_margin", a.isolated_position_margin_micro, core::Money{0});
  PROPR_J_GET(j, "cross_margin", a.cross_position_margin_micro, core::Money{0});
  PROPR_J_GET(j, "hwm", a.high_water_mark_micro, core::Money{0});
  PROPR_J_GET(j, "has_balance", a.has_balance, false);
  PROPR_J_GET(j, "has_upnl", a.has_upnl, false);
  PROPR_J_GET(j, "has_iso", a.has_iso, false);
  PROPR_J_GET(j, "has_xpm", a.has_xpm, false);
  PROPR_J_GET(j, "has_hwm", a.has_hwm, false);
  PROPR_J_GET(j, "at_ns", a.at_ns, core::Nanos{0});
}

inline void to_json(nlohmann::json& j, const IntentV1& i) {
  j = {{"v", i.v},
       {"intent_uuid", i.intent_uuid},
       {"strategy_name", i.strategy_name},
       {"kind", i.kind},
       {"asset_base", i.asset_base},
       {"qty_nano", i.quantity_nano},
       {"entry_price", i.suggested_entry_price_micro},
       {"risk_at_stop", i.risk_at_stop_micro},
       {"emitted_at_ns", i.emitted_at_ns}};
  if (i.stop_loss_price_micro) j["stop"] = *i.stop_loss_price_micro;
  if (i.take_profit_price_micro) j["tp"] = *i.take_profit_price_micro;
}
inline void from_json(const nlohmann::json& j, IntentV1& i) {
  PROPR_J_GET(j, "v", i.v, kVersion);
  PROPR_J_GET(j, "intent_uuid", i.intent_uuid, std::string{});
  PROPR_J_GET(j, "strategy_name", i.strategy_name, std::string{});
  PROPR_J_GET(j, "kind", i.kind, IntentKindV1::OpenLong);
  PROPR_J_GET(j, "asset_base", i.asset_base, std::string{});
  PROPR_J_GET(j, "qty_nano", i.quantity_nano, core::Qty{0});
  PROPR_J_GET(j, "entry_price", i.suggested_entry_price_micro, core::Price{0});
  PROPR_J_GET(j, "risk_at_stop", i.risk_at_stop_micro, core::Money{0});
  PROPR_J_GET(j, "emitted_at_ns", i.emitted_at_ns, core::Nanos{0});
  PROPR_J_OPT(j, "stop", i.stop_loss_price_micro);
  PROPR_J_OPT(j, "tp", i.take_profit_price_micro);
}

inline void to_json(nlohmann::json& j, const OrderCommandV1& c) {
  j = {{"v", c.v},
       {"command_uuid", c.command_uuid},
       {"intent_uuid", c.intent_uuid},
       {"order_group_id", c.order_group_id},
       {"entry_intent_id", c.entry_intent_id},
       {"asset_base", c.asset_base},
       {"entry_side", c.entry_side},
       {"position_side", c.position_side},
       {"qty_nano", c.quantity_nano},
       {"entry_limit_price", c.entry_limit_price_micro},
       {"expected_delta", c.expected_position_delta_micro},
       {"max_slippage_bps", c.max_slippage_bps},
       {"issued_at_ns", c.issued_at_ns},
       {"expires_at_ns", c.expires_at_ns},
       {"hmac_hex", c.hmac_hex}};
  if (c.stop_intent_id) j["stop_intent_id"] = *c.stop_intent_id;
  if (c.tp_intent_id) j["tp_intent_id"] = *c.tp_intent_id;
  if (c.stop_trigger_price_micro) j["stop_trigger"] = *c.stop_trigger_price_micro;
  if (c.tp_price_micro) j["tp_price"] = *c.tp_price_micro;
}
inline void from_json(const nlohmann::json& j, OrderCommandV1& c) {
  PROPR_J_GET(j, "v", c.v, kVersion);
  PROPR_J_GET(j, "command_uuid", c.command_uuid, std::string{});
  PROPR_J_GET(j, "intent_uuid", c.intent_uuid, std::string{});
  PROPR_J_GET(j, "order_group_id", c.order_group_id, std::string{});
  PROPR_J_GET(j, "entry_intent_id", c.entry_intent_id, std::string{});
  PROPR_J_GET(j, "asset_base", c.asset_base, std::string{});
  PROPR_J_GET(j, "entry_side", c.entry_side, std::string{});
  PROPR_J_GET(j, "position_side", c.position_side, std::string{});
  PROPR_J_GET(j, "qty_nano", c.quantity_nano, core::Qty{0});
  PROPR_J_GET(j, "entry_limit_price", c.entry_limit_price_micro, core::Price{0});
  PROPR_J_GET(j, "expected_delta", c.expected_position_delta_micro, core::Money{0});
  PROPR_J_GET(j, "max_slippage_bps", c.max_slippage_bps, 0);
  PROPR_J_GET(j, "issued_at_ns", c.issued_at_ns, core::Nanos{0});
  PROPR_J_GET(j, "expires_at_ns", c.expires_at_ns, core::Nanos{0});
  PROPR_J_GET(j, "hmac_hex", c.hmac_hex, std::string{});
  PROPR_J_OPT(j, "stop_intent_id", c.stop_intent_id);
  PROPR_J_OPT(j, "tp_intent_id", c.tp_intent_id);
  PROPR_J_OPT(j, "stop_trigger", c.stop_trigger_price_micro);
  PROPR_J_OPT(j, "tp_price", c.tp_price_micro);
}

inline void to_json(nlohmann::json& j, const RiskDecisionV1& d) {
  j = {{"v", d.v},
       {"intent_uuid", d.intent_uuid},
       {"outcome", d.outcome},
       {"reason", d.reason},
       {"detail", d.detail},
       {"decided_at_ns", d.decided_at_ns}};
  if (d.command) j["command"] = *d.command;
}
inline void from_json(const nlohmann::json& j, RiskDecisionV1& d) {
  PROPR_J_GET(j, "v", d.v, kVersion);
  PROPR_J_GET(j, "intent_uuid", d.intent_uuid, std::string{});
  PROPR_J_GET(j, "outcome", d.outcome, RiskOutcomeV1::Reject);
  PROPR_J_GET(j, "reason", d.reason, RiskReasonV1::InvalidIntent);
  PROPR_J_GET(j, "detail", d.detail, std::string{});
  PROPR_J_GET(j, "decided_at_ns", d.decided_at_ns, core::Nanos{0});
  PROPR_J_OPT(j, "command", d.command);
}

inline void to_json(nlohmann::json& j, const ExecutionReportV1& r) {
  j = {{"v", r.v},
       {"command_uuid", r.command_uuid},
       {"intent_uuid", r.intent_uuid},
       {"status", r.status},
       {"detail", r.detail},
       {"filled_qty_nano", r.filled_quantity_nano},
       {"avg_fill_price", r.average_fill_price_micro},
       {"fee", r.fee_micro},
       {"at_ns", r.at_ns}};
}
inline void from_json(const nlohmann::json& j, ExecutionReportV1& r) {
  PROPR_J_GET(j, "v", r.v, kVersion);
  PROPR_J_GET(j, "command_uuid", r.command_uuid, std::string{});
  PROPR_J_GET(j, "intent_uuid", r.intent_uuid, std::string{});
  PROPR_J_GET(j, "status", r.status, ExecutionStatusV1::NetworkError);
  PROPR_J_GET(j, "detail", r.detail, std::string{});
  PROPR_J_GET(j, "filled_qty_nano", r.filled_quantity_nano, core::Qty{0});
  PROPR_J_GET(j, "avg_fill_price", r.average_fill_price_micro, core::Price{0});
  PROPR_J_GET(j, "fee", r.fee_micro, core::Money{0});
  PROPR_J_GET(j, "at_ns", r.at_ns, core::Nanos{0});
}

inline void to_json(nlohmann::json& j, const FillV1& f) {
  j = {{"v", f.v},
       {"trade_id", f.trade_id},
       {"order_id", f.order_id},
       {"position_id", f.position_id},
       {"asset_base", f.asset_base},
       {"side", f.side},
       {"qty_nano", f.quantity_nano},
       {"price", f.price_micro},
       {"mark_at_order", f.mark_price_at_order_micro},
       {"fee", f.fee_micro},
       {"slippage", f.slippage_micro},
       {"at_ns", f.at_ns}};
}
inline void from_json(const nlohmann::json& j, FillV1& f) {
  PROPR_J_GET(j, "v", f.v, kVersion);
  PROPR_J_GET(j, "trade_id", f.trade_id, std::string{});
  PROPR_J_GET(j, "order_id", f.order_id, std::string{});
  PROPR_J_GET(j, "position_id", f.position_id, std::string{});
  PROPR_J_GET(j, "asset_base", f.asset_base, std::string{});
  PROPR_J_GET(j, "side", f.side, std::string{});
  PROPR_J_GET(j, "qty_nano", f.quantity_nano, core::Qty{0});
  PROPR_J_GET(j, "price", f.price_micro, core::Price{0});
  PROPR_J_GET(j, "mark_at_order", f.mark_price_at_order_micro, core::Price{0});
  PROPR_J_GET(j, "fee", f.fee_micro, core::Money{0});
  PROPR_J_GET(j, "slippage", f.slippage_micro, core::Money{0});
  PROPR_J_GET(j, "at_ns", f.at_ns, core::Nanos{0});
}

inline void to_json(nlohmann::json& j, const PositionUpdateV1& p) {
  j = {{"v", p.v},
       {"position_id", p.position_id},
       {"asset_base", p.asset_base},
       {"position_side", p.position_side},
       {"qty_nano", p.quantity_nano},
       {"entry_price", p.entry_price_micro},
       {"mark_price", p.mark_price_micro},
       {"upnl", p.unrealized_pnl_micro},
       {"rpnl", p.realized_pnl_micro},
       {"margin_used", p.margin_used_micro},
       {"status", p.status},
       {"at_ns", p.at_ns}};
}
inline void from_json(const nlohmann::json& j, PositionUpdateV1& p) {
  PROPR_J_GET(j, "v", p.v, kVersion);
  PROPR_J_GET(j, "position_id", p.position_id, std::string{});
  PROPR_J_GET(j, "asset_base", p.asset_base, std::string{});
  PROPR_J_GET(j, "position_side", p.position_side, std::string{});
  PROPR_J_GET(j, "qty_nano", p.quantity_nano, core::Qty{0});
  PROPR_J_GET(j, "entry_price", p.entry_price_micro, core::Price{0});
  PROPR_J_GET(j, "mark_price", p.mark_price_micro, core::Price{0});
  PROPR_J_GET(j, "upnl", p.unrealized_pnl_micro, core::Money{0});
  PROPR_J_GET(j, "rpnl", p.realized_pnl_micro, core::Money{0});
  PROPR_J_GET(j, "margin_used", p.margin_used_micro, core::Money{0});
  PROPR_J_GET(j, "status", p.status, PositionStatusV1::Updated);
  PROPR_J_GET(j, "at_ns", p.at_ns, core::Nanos{0});
}

inline void to_json(nlohmann::json& j, const KillSwitchTripV1& k) {
  j = {{"v", k.v},
       {"reason", k.reason},
       {"detail", k.detail},
       {"equity", k.equity_at_trip_micro},
       {"headroom", k.headroom_at_trip_micro},
       {"at_ns", k.at_ns}};
}
inline void from_json(const nlohmann::json& j, KillSwitchTripV1& k) {
  PROPR_J_GET(j, "v", k.v, kVersion);
  PROPR_J_GET(j, "reason", k.reason, KillSwitchReasonV1::ManualHalt);
  PROPR_J_GET(j, "detail", k.detail, std::string{});
  PROPR_J_GET(j, "equity", k.equity_at_trip_micro, core::Money{0});
  PROPR_J_GET(j, "headroom", k.headroom_at_trip_micro, core::Money{0});
  PROPR_J_GET(j, "at_ns", k.at_ns, core::Nanos{0});
}

inline void to_json(nlohmann::json& j, const ReconcileDivergenceV1& r) {
  j = {{"v", r.v}, {"field", r.field}, {"detail", r.detail}, {"at_ns", r.at_ns}};
}
inline void from_json(const nlohmann::json& j, ReconcileDivergenceV1& r) {
  PROPR_J_GET(j, "v", r.v, kVersion);
  PROPR_J_GET(j, "field", r.field, ReconcileFieldV1::Balance);
  PROPR_J_GET(j, "detail", r.detail, std::string{});
  PROPR_J_GET(j, "at_ns", r.at_ns, core::Nanos{0});
}

inline void to_json(nlohmann::json& j, const DailyResetV1& d) {
  j = {{"v", d.v}, {"equity_snapshot", d.equity_snapshot_micro}, {"boundary_ns", d.boundary_ns}};
}
inline void from_json(const nlohmann::json& j, DailyResetV1& d) {
  PROPR_J_GET(j, "v", d.v, kVersion);
  PROPR_J_GET(j, "equity_snapshot", d.equity_snapshot_micro, core::Money{0});
  PROPR_J_GET(j, "boundary_ns", d.boundary_ns, core::Nanos{0});
}

inline void to_json(nlohmann::json& j, const WsStatusV1& w) {
  j = {{"v", w.v}, {"ws_name", w.ws_name}, {"connected", w.connected}, {"at_ns", w.at_ns}};
}
inline void from_json(const nlohmann::json& j, WsStatusV1& w) {
  PROPR_J_GET(j, "v", w.v, kVersion);
  PROPR_J_GET(j, "ws_name", w.ws_name, std::string{});
  PROPR_J_GET(j, "connected", w.connected, false);
  PROPR_J_GET(j, "at_ns", w.at_ns, core::Nanos{0});
}

#undef PROPR_J_GET
#undef PROPR_J_OPT

}  // namespace propr::schemas::v1
