#include "propr/account/account.h"

namespace propr::account {

void Account::apply_account_update(core::Money balance,
                                   core::Money total_unrealized_pnl,
                                   core::Money isolated_position_margin,
                                   core::Money cross_position_margin,
                                   core::Money high_water_mark) {
  std::lock_guard<std::mutex> g(mu_);
  balance_ = balance;
  total_unrealized_pnl_ = total_unrealized_pnl;
  isolated_position_margin_ = isolated_position_margin;
  cross_position_margin_ = cross_position_margin;
  // High-water mark is monotonically non-decreasing.
  if (high_water_mark > high_water_mark_) high_water_mark_ = high_water_mark;
}

void Account::upsert_position(Position p) {
  std::lock_guard<std::mutex> g(mu_);
  positions_by_id_[p.id.value] = std::move(p);
}

void Account::erase_position(const core::PositionId& id) {
  std::lock_guard<std::mutex> g(mu_);
  positions_by_id_.erase(id.value);
}

core::Money Account::equity() const {
  std::lock_guard<std::mutex> g(mu_);
  // THE formula. Documented in CLAUDE.md and propr-docs. Never change without
  // re-reading the platform docs and updating both.
  return balance_ + total_unrealized_pnl_ + isolated_position_margin_;
}

core::Money Account::balance() const {
  std::lock_guard<std::mutex> g(mu_);
  return balance_;
}
core::Money Account::total_unrealized_pnl() const {
  std::lock_guard<std::mutex> g(mu_);
  return total_unrealized_pnl_;
}
core::Money Account::isolated_position_margin() const {
  std::lock_guard<std::mutex> g(mu_);
  return isolated_position_margin_;
}
core::Money Account::cross_position_margin() const {
  std::lock_guard<std::mutex> g(mu_);
  return cross_position_margin_;
}
core::Money Account::high_water_mark() const {
  std::lock_guard<std::mutex> g(mu_);
  return high_water_mark_;
}

std::vector<Position> Account::open_positions() const {
  std::lock_guard<std::mutex> g(mu_);
  std::vector<Position> out;
  out.reserve(positions_by_id_.size());
  for (const auto& [_, p] : positions_by_id_) {
    if (!p.is_flat()) out.push_back(p);
  }
  return out;
}

std::optional<Position> Account::position_for(const core::Asset& asset) const {
  std::lock_guard<std::mutex> g(mu_);
  for (const auto& [_, p] : positions_by_id_) {
    if (p.asset == asset && !p.is_flat()) return p;
  }
  return std::nullopt;
}

bool Account::has_margin_for(core::Money required_margin) const {
  std::lock_guard<std::mutex> g(mu_);
  const core::Money free = balance_ - cross_position_margin_ - isolated_position_margin_;
  return free >= required_margin;
}

}  // namespace propr::account
