#include "fake_account.h"

#include <algorithm>

namespace propr::backtest {

namespace {
core::Price slip(core::Price mark, int bps, propr::core::Side side) {
  const double factor = (bps / 10000.0);
  return side == propr::core::Side::Buy
             ? static_cast<core::Price>(mark * (1.0 + factor))
             : static_cast<core::Price>(mark * (1.0 - factor));
}
}  // namespace

void FakeAccount::simulate_entry(const strategy::Intent& intent, core::Price mark) {
  if (intent.kind != strategy::Intent::Kind::OpenLong &&
      intent.kind != strategy::Intent::Kind::OpenShort) {
    return;
  }
  const auto side = strategy::side_for(intent.kind);
  const core::Price fill = slip(mark, cfg_.slippage_bps, side);
  const core::Money notional = core::notional(fill, intent.quantity);
  const core::Money fee = (notional * cfg_.taker_fee_bps) / 10000;
  balance_ -= fee;
  auto& h = holdings_[intent.asset.base];
  const core::Money new_cost = h.entry_cost + notional;
  const core::Qty new_qty = h.qty + intent.quantity;
  h.entry_cost = new_cost;
  h.qty = new_qty;
  h.avg_entry = new_qty > 0
                    ? static_cast<core::Price>(new_cost * core::kNanoPerUnit / new_qty)
                    : 0;
}

void FakeAccount::mark_to_tick(const core::Asset& asset, core::Price mark) {
  auto it = holdings_.find(asset.base);
  if (it == holdings_.end() || it->second.qty == 0) return;
  const auto& h = it->second;
  const core::Money current_value = core::notional(mark, h.qty);
  const core::Money pnl = current_value - h.entry_cost;
  total_unrealized_ = pnl;
  const core::Money equity = balance_ + total_unrealized_;
  if (equity > hwm_) hwm_ = equity;
  mirror_.apply_account_update(balance_, total_unrealized_, 0, 0, hwm_);
}

}  // namespace propr::backtest
