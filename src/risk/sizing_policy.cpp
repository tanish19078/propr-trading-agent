#include "propr/risk/sizing_policy.h"

#include <algorithm>

namespace propr::risk {

namespace {
constexpr int kBpsScale = 10000;

core::Money mul_bps(core::Money base, int bps) {
  if (base <= 0 || bps <= 0) return 0;
  // (base * bps) / 10000, with __int128 to avoid overflow on large balances.
  __int128 product = static_cast<__int128>(base) * static_cast<__int128>(bps);
  return static_cast<core::Money>(product / kBpsScale);
}
}  // namespace

core::Money SizingPolicy::max_risk(core::Money equity,
                                   core::Money daily_headroom,
                                   core::Money overall_headroom) const {
  if (equity <= 0) return 0;
  if (daily_headroom <= 0) return 0;
  if (overall_headroom <= 0) return 0;

  const core::Money cap_equity = mul_bps(equity, caps_.max_risk_per_trade_bps);
  const core::Money cap_daily = mul_bps(daily_headroom, caps_.max_daily_headroom_use_bps);
  const core::Money cap_overall =
      mul_bps(overall_headroom, caps_.max_overall_headroom_use_bps);

  return std::min({cap_equity, cap_daily, cap_overall});
}

core::Qty SizingPolicy::max_qty(core::Money risk_budget,
                                core::Price entry,
                                core::Price stop) const {
  if (risk_budget <= 0) return 0;
  if (entry == stop) return 0;

  const core::Price gap = entry > stop ? (entry - stop) : (stop - entry);
  if (gap <= 0) return 0;

  // qty (nano-base) = risk_budget (micro-USDC) / (gap (micro-USDC/base) / nano-per-unit)
  //                 = risk_budget * nano-per-unit / gap
  __int128 num = static_cast<__int128>(risk_budget) * core::kNanoPerUnit;
  __int128 q = num / static_cast<__int128>(gap);
  if (q < 0) q = 0;
  return static_cast<core::Qty>(q);
}

}  // namespace propr::risk
