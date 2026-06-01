#include "propr/schemas/sign.h"

#include <sstream>

#include "propr/core/sha256.h"

namespace propr::schemas {

namespace {
void put(std::ostringstream& os, std::string_view label, std::string_view value) {
  os << label << '=' << value << '\n';
}
void put(std::ostringstream& os, std::string_view label, long long value) {
  os << label << '=' << value << '\n';
}
}  // namespace

std::string canonical_for_hmac(const v1::OrderCommandV1& c) {
  // Fixed field order. Any change here MUST bump kVersion.
  std::ostringstream os;
  os << "v=" << c.v << '\n';
  put(os, "command_uuid", c.command_uuid);
  put(os, "intent_uuid", c.intent_uuid);
  put(os, "order_group_id", c.order_group_id);
  put(os, "entry_intent_id", c.entry_intent_id);
  put(os, "stop_intent_id", c.stop_intent_id.value_or(""));
  put(os, "tp_intent_id", c.tp_intent_id.value_or(""));
  put(os, "asset_base", c.asset_base);
  put(os, "entry_side", c.entry_side);
  put(os, "position_side", c.position_side);
  put(os, "qty_nano", static_cast<long long>(c.quantity_nano));
  put(os, "entry_limit_price", static_cast<long long>(c.entry_limit_price_micro));
  put(os, "stop_trigger",
      static_cast<long long>(c.stop_trigger_price_micro.value_or(0)));
  put(os, "tp_price", static_cast<long long>(c.tp_price_micro.value_or(0)));
  put(os, "expected_delta", static_cast<long long>(c.expected_position_delta_micro));
  put(os, "max_slippage_bps", static_cast<long long>(c.max_slippage_bps));
  put(os, "issued_at_ns", static_cast<long long>(c.issued_at_ns));
  put(os, "expires_at_ns", static_cast<long long>(c.expires_at_ns));
  return os.str();
}

v1::OrderCommandV1 sign(const v1::OrderCommandV1& cmd, std::string_view secret) {
  auto c = cmd;
  c.hmac_hex = core::hmac_sha256_hex(secret, canonical_for_hmac(c));
  return c;
}

bool verify(const v1::OrderCommandV1& cmd, std::string_view secret) {
  if (cmd.hmac_hex.empty()) return false;
  const std::string expected = core::hmac_sha256_hex(secret, canonical_for_hmac(cmd));
  if (expected.size() != cmd.hmac_hex.size()) return false;
  // Constant-time compare to keep timing-side-channel hygiene.
  unsigned diff = 0;
  for (std::size_t i = 0; i < expected.size(); ++i) {
    diff |= static_cast<unsigned>(expected[i]) ^ static_cast<unsigned>(cmd.hmac_hex[i]);
  }
  return diff == 0;
}

}  // namespace propr::schemas
