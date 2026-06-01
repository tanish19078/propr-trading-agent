#include "propr/net/hyperliquid_ws.h"

#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

#include "propr/log/logger.h"

namespace propr::net {

namespace {
using json = nlohmann::json;
}  // namespace

HyperliquidWebSocket::HyperliquidWebSocket(Config cfg,
                                           core::EventBus& bus,
                                           const core::Clock& clock)
    : cfg_(std::move(cfg)), bus_(bus), clock_(clock),
      ws_(std::make_unique<ix::WebSocket>()) {}

HyperliquidWebSocket::~HyperliquidWebSocket() { stop(); }

void HyperliquidWebSocket::start() {
  if (running_.exchange(true)) return;
  ws_->setUrl(cfg_.url);
  ws_->setPingInterval(15);
  ws_->setHandshakeTimeout(10);

  ws_->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
    using ix::WebSocketMessageType;
    switch (msg->type) {
      case WebSocketMessageType::Open: on_open_(); break;
      case WebSocketMessageType::Message: handle_text_(msg->str); break;
      case WebSocketMessageType::Close:
        bus_.publish(core::WsDisconnect{.ws_name = "hyperliquid", .at_ns = clock_.now_ns()});
        break;
      default: break;
    }
  });
  ws_->start();
}

void HyperliquidWebSocket::stop() {
  if (!running_.exchange(false)) return;
  if (ws_) ws_->stop();
}

void HyperliquidWebSocket::on_open_() {
  last_event_ns_.store(clock_.now_ns(), std::memory_order_release);
  // Hyperliquid uses a JSON {"method":"subscribe","subscription":{"type":"allMids"}}
  json sub = {{"method", "subscribe"}, {"subscription", {{"type", "allMids"}}}};
  ws_->send(sub.dump());
  bus_.publish(core::WsReconnect{.ws_name = "hyperliquid", .at_ns = clock_.now_ns()});
}

void HyperliquidWebSocket::handle_text_(const std::string& msg) {
  last_event_ns_.store(clock_.now_ns(), std::memory_order_release);
  json j;
  try {
    j = json::parse(msg);
  } catch (...) {
    return;
  }
  if (j.value("channel", std::string{}) != "allMids") return;
  const auto& data = j["data"];
  if (!data.contains("mids") || !data["mids"].is_object()) return;
  for (const auto& base : cfg_.subscribe_bases) {
    if (!data["mids"].contains(base)) continue;
    const std::string mid = data["mids"][base].get<std::string>();
    core::Price p = 0;
    try {
      p = static_cast<core::Price>(std::stod(mid) * core::kMicroPerUnit);
    } catch (...) {
      continue;
    }
    core::MarketTick t;
    t.asset = {base};
    t.mark_price = p;
    t.at_ns = clock_.now_ns();
    bus_.publish(t);
  }
}

}  // namespace propr::net
