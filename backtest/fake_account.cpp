#include "fake_account.h"

#include <algorithm>

namespace propr::backtest {

namespace {
core::Price slip(core::Price mark, int bps, bool is_buy) {
  const double factor = (bps / 10000.0);
  return is_buy ? static_cast<core::Price>(mark * (1.0 + factor))
                : static_cast<core::Price>(mark * (1.0 - factor));
}
}  // namespace

void FakeAccount::simulate_entry(const schemas::v1::OrderCommandV1& cmd,
                                 core::Price mark) {
  const bool is_long = cmd.entry_side == "buy";
  const core::Price fill = slip(mark, cfg_.slippage_bps, is_long);
  const core::Money notional = core::notional(fill, cmd.quantity_nano);
  const core::Money fee = (notional * cfg_.taker_fee_bps) / 10000;
  balance_ -= fee;
  auto& h = holdings_[cmd.asset_base];
  const core::Money new_cost = h.entry_cost + notional;
  const core::Qty new_qty =
      h.qty + (is_long ? cmd.quantity_nano : -static_cast<core::Qty>(cmd.quantity_nano));
  h.entry_cost = new_cost;
  h.qty = new_qty;
  h.avg_entry = new_qty != 0
                    ? static_cast<core::Price>(new_cost * core::kNanoPerUnit / new_qty)
                    : 0;
}

core::Money FakeAccount::close(const std::string& base, core::Price fill) {
  auto it = holdings_.find(base);
  if (it == holdings_.end() || it->second.qty == 0) return 0;
  auto h = it->second;
  const bool is_short = h.qty < 0;
  const core::Qty exit_qty = is_short ? -h.qty : h.qty;
  const core::Money exit_value = core::notional(fill, h.qty);
  const core::Money pnl = exit_value - h.entry_cost;
  const core::Money fee =
      (core::notional(fill, exit_qty) * cfg_.taker_fee_bps) / 10000;
  balance_ += pnl - fee;
  holdings_.erase(it);
  total_unrealized_ = 0;
  ++closes_;
  mirror_.apply_account_update(balance_, 0, 0, 0, hwm_);
  return pnl - fee;
}

bool FakeAccount::has_position(const std::string& base) const {
  auto it = holdings_.find(base);
  return it != holdings_.end() && it->second.qty != 0;
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
