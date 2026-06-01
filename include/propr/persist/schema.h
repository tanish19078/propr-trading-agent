#pragma once

namespace propr::persist {

// SQLite schema. Bumped when columns change; Journal compares user_version on open
// and refuses to run on a mismatch (alembic-style migrations come later).
constexpr int kSchemaVersion = 1;

constexpr const char* kSchemaSql = R"sql(
PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS journal_events (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  at_ns INTEGER NOT NULL,
  kind TEXT NOT NULL,
  payload TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS journal_events_at_ns ON journal_events(at_ns);
CREATE INDEX IF NOT EXISTS journal_events_kind ON journal_events(kind);

CREATE TABLE IF NOT EXISTS intents (
  intent_uuid TEXT PRIMARY KEY,
  order_group_id TEXT NOT NULL,
  intent_ids_json TEXT NOT NULL,
  status TEXT NOT NULL,
  created_at_ns INTEGER NOT NULL,
  resolved_at_ns INTEGER
);

CREATE TABLE IF NOT EXISTS account_snapshots (
  at_ns INTEGER PRIMARY KEY,
  balance_micro INTEGER NOT NULL,
  unrealized_pnl_micro INTEGER NOT NULL,
  isolated_margin_micro INTEGER NOT NULL,
  equity_micro INTEGER NOT NULL,
  high_water_mark_micro INTEGER NOT NULL,
  reason TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS strategy_state (
  strategy_name TEXT PRIMARY KEY,
  blob BLOB NOT NULL,
  updated_at_ns INTEGER NOT NULL
);
)sql";

}  // namespace propr::persist
