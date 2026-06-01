#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace propr::core {

// All money values are int64 in micro-USDC. 1 USDC = 1'000'000. Never floats.
using Money = std::int64_t;
inline constexpr Money kMicroPerUnit = 1'000'000;

inline constexpr Money usdc(double whole) {
  return static_cast<Money>(whole * kMicroPerUnit);
}

// All prices are int64 in micro-units of quote per base. 1.0 USDC/BTC = 1'000'000.
using Price = std::int64_t;

// Quantities are int64 in nano-units of the base asset. 1 BTC = 1'000'000'000.
using Qty = std::int64_t;
inline constexpr Qty kNanoPerUnit = 1'000'000'000;

// Compute notional in micro-USDC from (price in micro-USDC/base) * (qty in nano-base).
inline constexpr Money notional(Price p, Qty q) {
  // (p * q) / kNanoPerUnit, in micro-USDC. Use __int128 to avoid overflow.
  __int128 result = (static_cast<__int128>(p) * static_cast<__int128>(q)) / kNanoPerUnit;
  return static_cast<Money>(result);
}

// Wall-clock instant as nanoseconds since unix epoch UTC.
using Nanos = std::int64_t;

enum class Side { Buy, Sell };
enum class PositionSide { Long, Short };
enum class OrderType {
  Market,
  Limit,
  StopMarket,
  StopLimit,
  TakeProfitMarket,
  TakeProfitLimit,
};
enum class TimeInForce { GTC, IOC, FOK, GTX };

// Strong typedefs for IDs — string-typed to match API. Wrap so we can't mix them up.
struct IntentId {
  std::string value;
  bool operator==(const IntentId&) const = default;
};
struct OrderGroupId {
  std::string value;
  bool operator==(const OrderGroupId&) const = default;
};
struct OrderId {
  std::string value;
  bool operator==(const OrderId&) const = default;
};
struct PositionId {
  std::string value;
  bool operator==(const PositionId&) const = default;
};
struct AccountId {
  std::string value;
  bool operator==(const AccountId&) const = default;
};
struct ChallengeAttemptId {
  std::string value;
  bool operator==(const ChallengeAttemptId&) const = default;
};

// Asset identifier — we only trade single-base perps on Hyperliquid, so it's just the base.
struct Asset {
  std::string base;  // "BTC", "ETH", etc.
  bool operator==(const Asset&) const = default;
};

inline constexpr std::string_view to_string(Side s) {
  return s == Side::Buy ? "buy" : "sell";
}
inline constexpr std::string_view to_string(PositionSide s) {
  return s == PositionSide::Long ? "long" : "short";
}
inline constexpr std::string_view to_string(OrderType t) {
  switch (t) {
    case OrderType::Market: return "market";
    case OrderType::Limit: return "limit";
    case OrderType::StopMarket: return "stop_market";
    case OrderType::StopLimit: return "stop_limit";
    case OrderType::TakeProfitMarket: return "take_profit_market";
    case OrderType::TakeProfitLimit: return "take_profit_limit";
  }
  return "unknown";
}
inline constexpr std::string_view to_string(TimeInForce t) {
  switch (t) {
    case TimeInForce::GTC: return "GTC";
    case TimeInForce::IOC: return "IOC";
    case TimeInForce::FOK: return "FOK";
    case TimeInForce::GTX: return "GTX";
  }
  return "GTC";
}

}  // namespace propr::core
