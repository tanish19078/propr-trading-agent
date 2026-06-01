#pragma once

#include "propr/core/types.h"

namespace propr::account {

struct Position {
  core::PositionId id;
  core::Asset asset;
  core::PositionSide side{core::PositionSide::Long};
  core::Qty quantity{0};
  core::Price entry_price{0};
  core::Price mark_price{0};
  core::Money unrealized_pnl{0};
  core::Money realized_pnl{0};
  core::Money margin_used{0};

  bool is_flat() const { return quantity == 0; }
};

}  // namespace propr::account
