#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "propr/core/types.h"

namespace propr::core {

// AccountUpdated — emitted on every Propr WS account.updated event and after every
// REST refetch. Mirrors the fields used to derive equity.
struct AccountUpdated {
  Money balance{0};
  Money total_unrealized_pnl{0};
  Money isolated_position_margin{0};
  Money cross_position_margin{0};
  Money high_water_mark{0};
  Nanos at_ns{0};
};

struct OrderUpdate {
  enum class Status {
    Created,
    Updated,
    Cancelled,
    Triggered,
    Filled,
    PartiallyFilled,
  };
  OrderId order_id;
  IntentId intent_id;
  OrderGroupId group_id;
  Status status{Status::Created};
  Asset asset;
  Side side{Side::Buy};
  OrderType type{OrderType::Market};
  Qty quantity{0};
  Qty cumulative_quantity{0};
  Price price{0};
  Price average_fill_price{0};
  Nanos at_ns{0};
};

struct PositionUpdate {
  enum class Status { Opened, Updated, Closed, Liquidated, TakeProfitHit, StopLossHit };
  PositionId position_id;
  Asset asset;
  PositionSide side{PositionSide::Long};
  Qty quantity{0};
  Price entry_price{0};
  Price mark_price{0};
  Money unrealized_pnl{0};
  Money realized_pnl{0};
  Money margin_used{0};
  Status status{Status::Opened};
  Nanos at_ns{0};
};

struct Fill {
  std::string trade_id;
  OrderId order_id;
  PositionId position_id;
  Asset asset;
  Side side{Side::Buy};
  Qty quantity{0};
  Price price{0};
  Price mark_price_at_order{0};
  Money fee{0};
  Money slippage{0};
  Nanos at_ns{0};
};

// MarketTick — public market data from Hyperliquid (or a backtest feeder).
struct MarketTick {
  Asset asset;
  Price mark_price{0};
  Price bid{0};
  Price ask{0};
  Nanos at_ns{0};
};

struct KillSwitchTrip {
  enum class Reason {
    FloatingLossExceeded,
    DailyLossExceeded,
    WsDisconnected,
    ReconcileDivergence,
    ManualHalt,
  };
  Reason reason{Reason::ManualHalt};
  std::string detail;
  Nanos at_ns{0};
};

struct ReconcileDivergence {
  enum class Field {
    Balance,
    Positions,
    OpenOrders,
  };
  Field field{Field::Balance};
  std::string detail;
  Nanos at_ns{0};
};

struct DailyReset {
  Money equity_snapshot{0};
  Nanos boundary_ns{0};
};

struct WsDisconnect {
  std::string ws_name;  // "propr" or "hyperliquid"
  Nanos at_ns{0};
};
struct WsReconnect {
  std::string ws_name;
  Nanos at_ns{0};
};

}  // namespace propr::core
