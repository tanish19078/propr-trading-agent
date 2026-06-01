#pragma once

#include <cstdint>
#include <string_view>

#include "propr/core/result.h"
#include "propr/core/types.h"

namespace propr::core {

// Parse a decimal string into a fixed-point micro-USDC integer.
//
// Accepts:
//   "0", "5000", "5000.0", "5000.123456", "-12.5", " 1.0 " (whitespace stripped),
//   "+3.14", "0.000001"
// Rejects (returns Error::JsonParseError):
//   empty string, "abc", "1.2.3", "5e3" (scientific notation), trailing junk,
//   more than 6 fractional digits (would silently truncate sub-micro precision)
//
// We deliberately avoid std::stod / strtod on the risk path so we never
// accidentally lose precision via binary float rounding.
Result<Money> parse_money_micro(std::string_view s);

// Same parsing, output is in nano-base units (1 BTC = 1e9 nano).
Result<Qty> parse_qty_nano(std::string_view s);

// Same parsing, output is in micro-USDC (used for prices). Identical to
// parse_money_micro but named for the Price type.
Result<Price> parse_price_micro(std::string_view s);

}  // namespace propr::core
