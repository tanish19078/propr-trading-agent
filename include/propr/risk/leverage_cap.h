#pragma once

#include "propr/core/types.h"

namespace propr::risk {

// Internal leverage caps, kept BELOW the platform max. Platform allows up to 5x on
// BTC/ETH but we choose to use less so adverse moves leave us slack.
class LeverageCap {
 public:
  LeverageCap(int max_btc_eth, int max_other_crypto)
      : max_btc_eth_(max_btc_eth), max_other_crypto_(max_other_crypto) {}

  int max_for(const core::Asset& a) const {
    if (a.base == "BTC" || a.base == "ETH") return max_btc_eth_;
    return max_other_crypto_;
  }

  // Returns true if (notional / equity) <= cap. notional and equity in micro-USDC.
  bool permits(const core::Asset& a, core::Money notional, core::Money equity) const {
    if (equity <= 0) return false;
    // notional <= cap * equity  ⇔  notional/equity <= cap.
    return notional <= static_cast<core::Money>(max_for(a)) * equity;
  }

 private:
  int max_btc_eth_;
  int max_other_crypto_;
};

}  // namespace propr::risk
