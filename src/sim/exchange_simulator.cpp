#include "propr/sim/exchange_simulator.h"

#include <algorithm>

namespace propr::sim {

using propr::schemas::v1::AccountSnapshotV1;
using propr::schemas::v1::ExecutionReportV1;
using propr::schemas::v1::ExecutionStatusV1;
using propr::schemas::v1::FillV1;
using propr::schemas::v1::OrderCommandV1;
using propr::schemas::v1::PositionStatusV1;
using propr::schemas::v1::PositionUpdateV1;
using propr::schemas::v1::TickV1;

ExchangeSimulator::ExchangeSimulator(SimConfig cfg, account::Account& mirror,
                                     core::EventBus& bus, core::Clock& clock)
    : cfg_(cfg),
      mirror_(mirror),
      bus_(bus),
      clock_(clock),
      rng_(cfg.seed),
      balance_(cfg.starting_balance),
      hwm_(cfg.starting_balance) {
  mirror_.set_id(core::AccountId{cfg_.account_id});
  mirror_.apply_account_update(balance_, 0, 0, 0, hwm_);
  publish_account_snapshot_("boot");
}

double ExchangeSimulator::rng01_() {
  std::uniform_real_distribution<double> d(0.0, 1.0);
  return d(rng_);
}

core::Money ExchangeSimulator::balance() const {
  std::lock_guard<std::mutex> g(mu_);
  return balance_;
}
core::Money ExchangeSimulator::equity() const { return mirror_.equity(); }

void ExchangeSimulator::tick(const TickV1& t) {
  std::lock_guard<std::mutex> g(mu_);
  last_mark_[t.base] = t.mark_micro;

  // Release any pending fills whose timer has elapsed.
  release_due_fills_();

  // Mark-to-market all holdings.
  core::Money total_upnl = 0;
  for (auto& [asset, h] : holdings_) {
    auto it = last_mark_.find(asset);
    if (it == last_mark_.end() || h.qty == 0) continue;
    const core::Money current_value = core::notional(it->second, h.qty);
    total_upnl += current_value - h.entry_cost;
  }
  mirror_.apply_account_update(balance_, total_upnl, 0, 0, hwm_);
  const core::Money eq = balance_ + total_upnl;
  if (eq > hwm_) {
    hwm_ = eq;
    mirror_.apply_account_update(balance_, total_upnl, 0, 0, hwm_);
  }

  // Snapshot publishing: respect drop/duplicate/stale knobs.
  AccountSnapshotV1 s;
  s.account_id = cfg_.account_id;
  s.source = "sim";
  s.balance_micro = balance_;
  s.total_unrealized_pnl_micro = total_upnl;
  s.high_water_mark_micro = hwm_;
  s.has_balance = s.has_upnl = s.has_hwm = true;
  s.at_ns = t.at_ns;
  stale_buffer_.push_back(s);
  while (static_cast<int>(stale_buffer_.size()) >
         std::max(1, cfg_.stale_snapshot_ticks + 1)) {
    AccountSnapshotV1 publish = stale_buffer_.front();
    stale_buffer_.pop_front();
    if (rng01_() < cfg_.drop_snapshot_probability) continue;
    bus_.publish(core::AccountUpdated{
        .balance = publish.balance_micro,
        .total_unrealized_pnl = publish.total_unrealized_pnl_micro,
        .isolated_position_margin = 0,
        .cross_position_margin = 0,
        .high_water_mark = publish.high_water_mark_micro,
        .at_ns = publish.at_ns,
    });
    if (rng01_() < cfg_.duplicate_snapshot_probability) {
      bus_.publish(core::AccountUpdated{
          .balance = publish.balance_micro,
          .total_unrealized_pnl = publish.total_unrealized_pnl_micro,
          .isolated_position_margin = 0,
          .cross_position_margin = 0,
          .high_water_mark = publish.high_water_mark_micro,
          .at_ns = publish.at_ns,
      });
    }
  }
}

void ExchangeSimulator::release_due_fills_() {
  std::vector<PendingFill> still_pending;
  for (auto& p : pending_) {
    if (--p.releases_in <= 0) {
      auto it = last_mark_.find(p.cmd.asset_base);
      if (it == last_mark_.end()) {
        still_pending.push_back(p);
        continue;
      }
      const core::Qty fill_qty =
          p.partial ? (p.cmd.quantity_nano / 2) : p.cmd.quantity_nano;
      apply_fill_(p.cmd, fill_qty, it->second);
    } else {
      still_pending.push_back(p);
    }
  }
  pending_ = std::move(still_pending);
}

void ExchangeSimulator::apply_fill_(const OrderCommandV1& cmd, core::Qty qty,
                                    core::Price mark) {
  const bool is_buy = cmd.entry_side == "buy";
  const double factor = cfg_.slippage_bps / 10000.0;
  const core::Price fill_price = is_buy
                                     ? static_cast<core::Price>(mark * (1.0 + factor))
                                     : static_cast<core::Price>(mark * (1.0 - factor));
  const core::Money notional = core::notional(fill_price, qty);
  const core::Money fee = (notional * cfg_.taker_fee_bps) / 10000;
  balance_ -= fee;
  auto& h = holdings_[cmd.asset_base];
  h.entry_cost += notional;
  h.qty += qty;
  h.avg_entry = h.qty > 0
                    ? static_cast<core::Price>(h.entry_cost * core::kNanoPerUnit / h.qty)
                    : 0;

  FillV1 f;
  f.trade_id = "sim_trade_" + std::to_string(fills_emitted_++);
  f.order_id = cmd.command_uuid;
  f.position_id = "sim_pos_" + cmd.asset_base;
  f.asset_base = cmd.asset_base;
  f.side = cmd.entry_side;
  f.quantity_nano = qty;
  f.price_micro = fill_price;
  f.mark_price_at_order_micro = mark;
  f.fee_micro = fee;
  f.slippage_micro = is_buy ? (fill_price - mark) : (mark - fill_price);
  f.at_ns = clock_.now_ns();
  bus_.publish(core::Fill{
      .trade_id = f.trade_id,
      .order_id = core::OrderId{f.order_id},
      .position_id = core::PositionId{f.position_id},
      .asset = {f.asset_base},
      .side = is_buy ? core::Side::Buy : core::Side::Sell,
      .quantity = qty,
      .price = fill_price,
      .mark_price_at_order = mark,
      .fee = fee,
      .slippage = f.slippage_micro,
      .at_ns = f.at_ns,
  });
  publish_position_update_(cmd.asset_base);
}

void ExchangeSimulator::publish_account_snapshot_(const char* source) {
  bus_.publish(core::AccountUpdated{
      .balance = balance_,
      .total_unrealized_pnl = mirror_.total_unrealized_pnl(),
      .isolated_position_margin = 0,
      .cross_position_margin = 0,
      .high_water_mark = hwm_,
      .at_ns = clock_.now_ns(),
  });
  (void)source;
}

void ExchangeSimulator::publish_position_update_(const std::string& asset) {
  auto it = holdings_.find(asset);
  if (it == holdings_.end()) return;
  const auto& h = it->second;
  bus_.publish(core::PositionUpdate{
      .position_id = core::PositionId{"sim_pos_" + asset},
      .asset = {asset},
      .side = core::PositionSide::Long,
      .quantity = h.qty,
      .entry_price = h.avg_entry,
      .mark_price = last_mark_[asset],
      .unrealized_pnl =
          core::notional(last_mark_[asset], h.qty) - h.entry_cost,
      .realized_pnl = 0,
      .margin_used = 0,
      .status = h.qty == 0 ? core::PositionUpdate::Status::Closed
                            : core::PositionUpdate::Status::Updated,
      .at_ns = clock_.now_ns(),
  });
}

ExecutionReportV1 ExchangeSimulator::place(const OrderCommandV1& cmd) {
  std::lock_guard<std::mutex> g(mu_);
  ExecutionReportV1 r;
  r.command_uuid = cmd.command_uuid;
  r.intent_uuid = cmd.intent_uuid;
  r.at_ns = clock_.now_ns();

  if (rng01_() < cfg_.rate_limit_probability) {
    r.status = ExecutionStatusV1::NetworkError;
    r.detail = "sim_429";
    ++rejections_emitted_;
    return r;
  }
  if (rng01_() < cfg_.rejection_probability) {
    r.status = ExecutionStatusV1::RejectedNotLive;
    r.detail = "sim_rejected";
    ++rejections_emitted_;
    return r;
  }
  if (rng01_() < cfg_.fill_skip_probability) {
    r.status = ExecutionStatusV1::Accepted;
    r.detail = "sim_skipped_fill";
    return r;
  }

  const bool partial = rng01_() < cfg_.partial_fill_probability;
  pending_.push_back({cmd, std::max(1, cfg_.fill_latency_ticks), partial});
  r.status = ExecutionStatusV1::Accepted;
  return r;
}

ExecutionReportV1 ExchangeSimulator::cancel_all() {
  std::lock_guard<std::mutex> g(mu_);
  pending_.clear();
  ExecutionReportV1 r;
  r.status = ExecutionStatusV1::Cancelled;
  r.detail = "sim_cancel_all";
  r.at_ns = clock_.now_ns();
  return r;
}

ExecutionReportV1 ExchangeSimulator::close(const std::string& asset_base,
                                           const std::string& position_side,
                                           core::Qty quantity_nano) {
  std::lock_guard<std::mutex> g(mu_);
  ExecutionReportV1 r;
  r.at_ns = clock_.now_ns();
  auto it = last_mark_.find(asset_base);
  if (it == last_mark_.end()) {
    r.status = ExecutionStatusV1::NetworkError;
    r.detail = "no mark";
    return r;
  }
  auto& h = holdings_[asset_base];
  const core::Qty close_qty = std::min(quantity_nano, h.qty);
  const core::Money current_value = core::notional(it->second, close_qty);
  const core::Money realized =
      current_value -
      (h.entry_cost * close_qty / std::max<core::Qty>(h.qty, 1));
  balance_ += current_value;
  const core::Money fee = (current_value * cfg_.taker_fee_bps) / 10000;
  balance_ -= fee;
  h.qty -= close_qty;
  h.entry_cost -= h.entry_cost * close_qty / std::max<core::Qty>(h.qty + close_qty, 1);
  (void)position_side;
  (void)realized;
  r.status = ExecutionStatusV1::Filled;
  r.filled_quantity_nano = close_qty;
  r.average_fill_price_micro = it->second;
  r.fee_micro = fee;
  publish_position_update_(asset_base);
  return r;
}

}  // namespace propr::sim
