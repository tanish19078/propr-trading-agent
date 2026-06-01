#include "propr/net/propr_ws.h"

#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

#include "propr/core/decimal.h"
#include "propr/log/logger.h"

namespace propr::net {

namespace {
using json = nlohmann::json;

core::Money money_from(const json& j, const char* k) {
  if (!j.contains(k) || j[k].is_null()) return 0;
  std::string s = j[k].is_string() ? j[k].get<std::string>() : j[k].dump();
  auto r = core::parse_money_micro(s);
  return r ? *r : 0;
}

core::Qty qty_from(const json& j, const char* k) {
  if (!j.contains(k) || j[k].is_null()) return 0;
  std::string s = j[k].is_string() ? j[k].get<std::string>() : j[k].dump();
  auto r = core::parse_qty_nano(s);
  return r ? *r : 0;
}

core::Side side_from(const json& d) {
  return d.value("side", std::string{"buy"}) == "sell" ? core::Side::Sell
                                                       : core::Side::Buy;
}
core::PositionSide pos_side_from(const json& d) {
  return d.value("positionSide", std::string{"long"}) == "short"
             ? core::PositionSide::Short
             : core::PositionSide::Long;
}
std::string asset_from(const json& d) {
  if (d.contains("base") && d["base"].is_string()) return d["base"];
  if (d.contains("asset") && d["asset"].is_string()) return d["asset"];
  return {};
}
}  // namespace

ProprWebSocket::ProprWebSocket(Config cfg, core::EventBus& bus, const core::Clock& clock)
    : cfg_(std::move(cfg)),
      bus_(bus),
      clock_(clock),
      ws_(std::make_unique<ix::WebSocket>()) {}

ProprWebSocket::~ProprWebSocket() { stop(); }

void ProprWebSocket::start() {
  if (running_.exchange(true)) return;
  ix::WebSocketHttpHeaders headers;
  headers["X-API-Key"] = cfg_.api_key;
  ws_->setUrl(cfg_.url);
  ws_->setExtraHeaders(headers);
  ws_->setPingInterval(20);
  ws_->setHandshakeTimeout(10);

  ws_->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
    using ix::WebSocketMessageType;
    switch (msg->type) {
      case WebSocketMessageType::Message:
        handle_text_(msg->str);
        break;
      case WebSocketMessageType::Open:
        last_event_ns_.store(clock_.now_ns(), std::memory_order_release);
        PROPR_LOG_INFO(R"({"propr_ws_connected":true})");
        bus_.publish(core::WsReconnect{.ws_name = "propr", .at_ns = clock_.now_ns()});
        break;
      case WebSocketMessageType::Close:
        PROPR_LOG_WARN(R"({"propr_ws_disconnected":true})");
        bus_.publish(core::WsDisconnect{.ws_name = "propr", .at_ns = clock_.now_ns()});
        break;
      case WebSocketMessageType::Error:
        PROPR_LOG_ERROR(std::string{R"({"propr_ws_error":")"} +
                        msg->errorInfo.reason + R"("})");
        break;
      default:
        break;
    }
  });

  ws_->start();
}

void ProprWebSocket::stop() {
  if (!running_.exchange(false)) return;
  if (ws_) ws_->stop();
}

void ProprWebSocket::handle_text_(const std::string& msg) {
  last_event_ns_.store(clock_.now_ns(), std::memory_order_release);
  json j;
  try {
    j = json::parse(msg);
  } catch (...) {
    return;
  }
  const auto type = j.value("type", std::string{});
  const auto& d = j["data"];

  if (type == "account.updated") {
    core::AccountUpdated ev;
    ev.balance = money_from(d, "balance");
    ev.total_unrealized_pnl = money_from(d, "totalUnrealizedPnl");
    ev.isolated_position_margin = money_from(d, "isolatedPositionMargin");
    ev.cross_position_margin = money_from(d, "crossPositionMargin");
    ev.high_water_mark = money_from(d, "highWaterMark");
    ev.at_ns = clock_.now_ns();
    bus_.publish(ev);
    return;
  }

  if (type == "trade.created") {
    core::Fill f;
    f.trade_id = d.value("tradeId", d.value("id", std::string{}));
    f.order_id = core::OrderId{d.value("orderId", std::string{})};
    f.position_id = core::PositionId{d.value("positionId", std::string{})};
    f.asset.base = asset_from(d);
    f.side = side_from(d);
    f.quantity = qty_from(d, "quantity");
    f.price = money_from(d, "price");
    f.mark_price_at_order = money_from(d, "markPriceAtOrder");
    f.fee = money_from(d, "fee");
    f.slippage = money_from(d, "slippage");
    f.at_ns = clock_.now_ns();
    bus_.publish(f);
    return;
  }

  if (type == "position.opened" || type == "position.updated" ||
      type == "position.closed" || type == "position.liquidated" ||
      type == "position.take_profit.hit" || type == "position.stop_loss.hit") {
    core::PositionUpdate p;
    p.position_id = core::PositionId{d.value("positionId", d.value("id", std::string{}))};
    p.asset.base = asset_from(d);
    p.side = pos_side_from(d);
    p.quantity = qty_from(d, "quantity");
    p.entry_price = money_from(d, "entryPrice");
    p.mark_price = money_from(d, "markPrice");
    p.unrealized_pnl = money_from(d, "unrealizedPnl");
    p.realized_pnl = money_from(d, "realizedPnl");
    p.margin_used = money_from(d, "marginUsed");
    p.at_ns = clock_.now_ns();
    if (type == "position.opened")
      p.status = core::PositionUpdate::Status::Opened;
    else if (type == "position.closed")
      p.status = core::PositionUpdate::Status::Closed;
    else if (type == "position.liquidated")
      p.status = core::PositionUpdate::Status::Liquidated;
    else if (type == "position.take_profit.hit")
      p.status = core::PositionUpdate::Status::TakeProfitHit;
    else if (type == "position.stop_loss.hit")
      p.status = core::PositionUpdate::Status::StopLossHit;
    else
      p.status = core::PositionUpdate::Status::Updated;
    bus_.publish(p);
    return;
  }

  // order.* events are mostly observability for now; we don't model an
  // open-order book in memory yet. Log them for the journal trail.
  if (type.rfind("order.", 0) == 0) {
    PROPR_LOG_DEBUG(std::string{R"({"propr_order_event":")"} + type + R"("})");
    return;
  }
}

}  // namespace propr::net
