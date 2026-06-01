#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "propr/account/position.h"
#include "propr/core/types.h"

namespace propr::account {

// In-memory mirror of /accounts/{id}. The single source of truth on the risk path.
// Updated from Propr WS account.updated and after every REST reconcile.
class Account {
 public:
  Account() = default;

  void set_id(core::AccountId id) { id_ = std::move(id); }
  const core::AccountId& id() const { return id_; }

  // Setters used by the WS dispatcher and the reconciler.
  void apply_account_update(core::Money balance,
                            core::Money total_unrealized_pnl,
                            core::Money isolated_position_margin,
                            core::Money cross_position_margin,
                            core::Money high_water_mark);

  void upsert_position(Position p);
  void erase_position(const core::PositionId& id);

  // ─── derived state ──────────────────────────────────────────────────────────
  // The most important formula in the project. Mirrored client-side because
  // Propr has no equity field on /accounts.
  core::Money equity() const;

  core::Money balance() const;
  core::Money total_unrealized_pnl() const;
  core::Money isolated_position_margin() const;
  core::Money cross_position_margin() const;
  core::Money high_water_mark() const;

  std::vector<Position> open_positions() const;
  std::optional<Position> position_for(const core::Asset& asset) const;

  // True if `required_margin` (in micro-USDC) fits inside `availableBalance`-proxy:
  // balance - cross_position_margin - isolated_position_margin.
  bool has_margin_for(core::Money required_margin) const;

 private:
  mutable std::mutex mu_;
  core::AccountId id_;
  core::Money balance_{0};
  core::Money total_unrealized_pnl_{0};
  core::Money isolated_position_margin_{0};
  core::Money cross_position_margin_{0};
  core::Money high_water_mark_{0};
  std::unordered_map<std::string, Position> positions_by_id_;
};

}  // namespace propr::account
