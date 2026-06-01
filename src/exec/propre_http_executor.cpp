#include "propr/exec/propre_http_executor.h"

#include <nlohmann/json.hpp>

namespace propr::exec {

namespace {
using json = nlohmann::json;
using propr::schemas::v1::ExecutionReportV1;
using propr::schemas::v1::ExecutionStatusV1;
using propr::schemas::v1::OrderCommandV1;

json build_payload(const OrderCommandV1& c) {
  json orders = json::array();
  json entry = {
      {"intentId", c.entry_intent_id},
      {"exchange", "hyperliquid"},
      {"productType", "perp"},
      {"type", "limit"},
      {"timeInForce", "IOC"},
      {"side", c.entry_side},
      {"positionSide", c.position_side},
      {"asset", c.asset_base},
      {"base", c.asset_base},
      {"quote", "USDC"},
      {"quantity", c.quantity_nano},
      {"price", c.entry_limit_price_micro},
      {"reduceOnly", false},
  };
  orders.push_back(entry);

  if (c.stop_intent_id && c.stop_trigger_price_micro) {
    const auto exit_side = (c.entry_side == "buy") ? "sell" : "buy";
    json stop = {
        {"intentId", *c.stop_intent_id},
        {"exchange", "hyperliquid"},
        {"productType", "perp"},
        {"type", "stop_market"},
        {"side", exit_side},
        {"positionSide", c.position_side},
        {"asset", c.asset_base},
        {"base", c.asset_base},
        {"quote", "USDC"},
        {"quantity", c.quantity_nano},
        {"triggerPrice", *c.stop_trigger_price_micro},
        {"reduceOnly", true},
        {"closePosition", true},
    };
    orders.push_back(stop);
  }
  if (c.tp_intent_id && c.tp_price_micro) {
    const auto exit_side = (c.entry_side == "buy") ? "sell" : "buy";
    json tp = {
        {"intentId", *c.tp_intent_id},
        {"exchange", "hyperliquid"},
        {"productType", "perp"},
        {"type", "take_profit_limit"},
        {"timeInForce", "GTC"},
        {"side", exit_side},
        {"positionSide", c.position_side},
        {"asset", c.asset_base},
        {"base", c.asset_base},
        {"quote", "USDC"},
        {"quantity", c.quantity_nano},
        {"price", *c.tp_price_micro},
        {"triggerPrice", *c.tp_price_micro},
        {"reduceOnly", true},
    };
    orders.push_back(tp);
  }
  return json{{"orderGroupId", c.order_group_id}, {"orders", orders}};
}
}  // namespace

ExecutionReportV1 PropreHttpExecutor::place(const OrderCommandV1& cmd) {
  ExecutionReportV1 r;
  r.command_uuid = cmd.command_uuid;
  r.intent_uuid = cmd.intent_uuid;
  r.at_ns = clock_.now_ns();

  const std::string path = "/accounts/" + account_.id().value + "/orders";
  auto resp = http_.post(path, build_payload(cmd));
  if (!resp) {
    r.status = ExecutionStatusV1::NetworkError;
    r.detail = resp.error().message;
    return r;
  }
  r.status = ExecutionStatusV1::Accepted;
  return r;
}

ExecutionReportV1 PropreHttpExecutor::cancel_all() {
  ExecutionReportV1 r;
  r.at_ns = clock_.now_ns();

  // Try the bulk cancel-all endpoint first (may not exist on all Propr deployments).
  const std::string bulk_path = "/accounts/" + account_.id().value + "/orders/cancel-all";
  auto bulk_resp = http_.post_reserved(bulk_path, nlohmann::json::object());
  if (bulk_resp) {
    r.status = ExecutionStatusV1::Cancelled;
    r.detail = "bulk_cancel_all";
    return r;
  }

  // Fallback: fetch open orders and cancel individually.
  std::unordered_map<std::string, std::string> query{{"status", "open"}};
  auto orders_resp = http_.get("/accounts/" + account_.id().value + "/orders", query);
  if (!orders_resp) {
    r.status = ExecutionStatusV1::NetworkError;
    r.detail = "cancel_all_fallback_failed: " + orders_resp.error().message;
    return r;
  }

  int cancelled = 0;
  if (orders_resp->contains("data") && (*orders_resp)["data"].is_array()) {
    for (const auto& order : (*orders_resp)["data"]) {
      if (!order.contains("orderId")) continue;
      const std::string order_id = order["orderId"];
      const std::string cancel_path = "/accounts/" + account_.id().value + "/orders/" + order_id;
      auto cancel_resp = http_.delete_reserved(cancel_path);
      if (cancel_resp) ++cancelled;
    }
  }

  r.status = ExecutionStatusV1::Cancelled;
  r.detail = "cancelled_" + std::to_string(cancelled) + "_orders";
  return r;
}

ExecutionReportV1 PropreHttpExecutor::close(const std::string& asset_base,
                                            const std::string& position_side,
                                            core::Qty quantity_nano) {
  ExecutionReportV1 r;
  r.at_ns = clock_.now_ns();
  const std::string path = "/accounts/" + account_.id().value + "/orders";
  const auto exit_side = position_side == "long" ? "sell" : "buy";
  json body = {
      {"intentId", ulid_.next()},
      {"exchange", "hyperliquid"},
      {"productType", "perp"},
      {"type", "market"},
      {"timeInForce", "IOC"},
      {"side", exit_side},
      {"positionSide", position_side},
      {"asset", asset_base},
      {"base", asset_base},
      {"quote", "USDC"},
      {"quantity", quantity_nano},
      {"reduceOnly", true},
      {"closePosition", true},
  };
  auto resp = http_.post_reserved(path, body);
  if (!resp) {
    r.status = ExecutionStatusV1::NetworkError;
    r.detail = resp.error().message;
    return r;
  }
  r.status = ExecutionStatusV1::Accepted;
  return r;
}

}  // namespace propr::exec
