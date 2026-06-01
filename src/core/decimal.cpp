#include "propr/core/decimal.h"

#include <cctype>
#include <limits>

namespace propr::core {

namespace {

constexpr std::int64_t pow10(int n) {
  std::int64_t out = 1;
  for (int i = 0; i < n; ++i) out *= 10;
  return out;
}

// Generic decimal-string -> int64 with `scale` implied digits after the point.
// e.g. scale=6 means "1" -> 1_000_000, "0.5" -> 500_000.
Result<std::int64_t> parse_scaled(std::string_view s, int scale) {
  // Trim whitespace.
  std::size_t i = 0;
  std::size_t end = s.size();
  while (i < end && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  while (end > i && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
  if (i == end) {
    return make_error(Error::Code::JsonParseError, "empty decimal string");
  }
  bool negative = false;
  if (s[i] == '+' || s[i] == '-') {
    negative = (s[i] == '-');
    ++i;
    if (i == end) {
      return make_error(Error::Code::JsonParseError, "lone sign");
    }
  }
  std::int64_t whole = 0;
  bool seen_digit = false;
  while (i < end && std::isdigit(static_cast<unsigned char>(s[i]))) {
    seen_digit = true;
    const int d = s[i] - '0';
    if (whole > (std::numeric_limits<std::int64_t>::max() / 10) - 1) {
      return make_error(Error::Code::JsonParseError, "decimal overflow");
    }
    whole = whole * 10 + d;
    ++i;
  }
  std::int64_t frac = 0;
  int frac_digits = 0;
  if (i < end && s[i] == '.') {
    ++i;
    while (i < end && std::isdigit(static_cast<unsigned char>(s[i]))) {
      seen_digit = true;
      if (frac_digits < scale) {
        frac = frac * 10 + (s[i] - '0');
      } else if (s[i] != '0') {
        // Sub-`scale` digits would silently truncate. We refuse instead.
        return make_error(Error::Code::JsonParseError,
                           "more fractional digits than scale supports");
      }
      ++frac_digits;
      ++i;
    }
  }
  if (i != end) {
    // trailing non-digit (e.g. "5e3", "1.2x") - reject
    return make_error(Error::Code::JsonParseError, "trailing junk in decimal");
  }
  if (!seen_digit) {
    return make_error(Error::Code::JsonParseError, "no digits in decimal");
  }
  // Pad frac to `scale` digits.
  if (frac_digits < scale) {
    frac *= pow10(scale - frac_digits);
  }
  std::int64_t total;
  __int128 product = static_cast<__int128>(whole) * pow10(scale) + frac;
  if (product > std::numeric_limits<std::int64_t>::max()) {
    return make_error(Error::Code::JsonParseError, "decimal overflow after scale");
  }
  total = static_cast<std::int64_t>(product);
  if (negative) total = -total;
  return total;
}

}  // namespace

Result<Money> parse_money_micro(std::string_view s) {
  return parse_scaled(s, 6);
}
Result<Price> parse_price_micro(std::string_view s) {
  return parse_scaled(s, 6);
}
Result<Qty> parse_qty_nano(std::string_view s) {
  return parse_scaled(s, 9);
}

}  // namespace propr::core
