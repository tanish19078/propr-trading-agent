#include "propr/persist/journal.h"

#include <SQLiteCpp/SQLiteCpp.h>

#include <stdexcept>

#include "propr/persist/schema.h"

namespace propr::persist {

Journal::Journal(const std::string& path)
    : db_(std::make_unique<SQLite::Database>(
          path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)) {
  db_->exec(kSchemaSql);

  // Use SQLite user_version as a cheap schema-version pin.
  int v = db_->execAndGet("PRAGMA user_version").getInt();
  if (v == 0) {
    db_->exec("PRAGMA user_version = " + std::to_string(kSchemaVersion));
  } else if (v != kSchemaVersion) {
    throw std::runtime_error("journal schema version mismatch (expected " +
                             std::to_string(kSchemaVersion) + ", got " +
                             std::to_string(v) + ")");
  }
}

Journal::~Journal() = default;

void Journal::write_event(core::Nanos at_ns,
                          std::string_view kind,
                          std::string_view json_payload) {
  SQLite::Statement q(*db_,
                      "INSERT INTO journal_events(at_ns, kind, payload) VALUES (?,?,?)");
  q.bind(1, static_cast<std::int64_t>(at_ns));
  q.bind(2, std::string(kind));
  q.bind(3, std::string(json_payload));
  q.exec();
}

void Journal::write_intent(const IntentRecord& r) {
  SQLite::Statement q(
      *db_,
      "INSERT OR REPLACE INTO intents"
      " (intent_uuid, order_group_id, intent_ids_json, status, created_at_ns, resolved_at_ns)"
      " VALUES (?,?,?,?,?,?)");
  q.bind(1, r.intent_uuid);
  q.bind(2, r.order_group_id);
  q.bind(3, r.intent_ids_json);
  q.bind(4, r.status);
  q.bind(5, static_cast<std::int64_t>(r.created_at_ns));
  if (r.resolved_at_ns) {
    q.bind(6, static_cast<std::int64_t>(*r.resolved_at_ns));
  } else {
    q.bind(6);  // null
  }
  q.exec();
}

void Journal::update_intent_status(const std::string& intent_uuid,
                                   const std::string& status,
                                   core::Nanos resolved_at_ns) {
  SQLite::Statement q(
      *db_,
      "UPDATE intents SET status = ?, resolved_at_ns = ? WHERE intent_uuid = ?");
  q.bind(1, status);
  q.bind(2, static_cast<std::int64_t>(resolved_at_ns));
  q.bind(3, intent_uuid);
  q.exec();
}

std::vector<Journal::IntentRecord> Journal::unresolved_intents() {
  std::vector<IntentRecord> out;
  SQLite::Statement q(*db_,
                      "SELECT intent_uuid, order_group_id, intent_ids_json, status, "
                      "created_at_ns, resolved_at_ns FROM intents "
                      "WHERE resolved_at_ns IS NULL ORDER BY created_at_ns");
  while (q.executeStep()) {
    IntentRecord r;
    r.intent_uuid = q.getColumn(0).getString();
    r.order_group_id = q.getColumn(1).getString();
    r.intent_ids_json = q.getColumn(2).getString();
    r.status = q.getColumn(3).getString();
    r.created_at_ns = q.getColumn(4).getInt64();
    out.push_back(std::move(r));
  }
  return out;
}

void Journal::write_snapshot(const AccountSnapshot& s) {
  SQLite::Statement q(*db_,
                      "INSERT OR REPLACE INTO account_snapshots"
                      " (at_ns, balance_micro, unrealized_pnl_micro, isolated_margin_micro,"
                      "  equity_micro, high_water_mark_micro, reason)"
                      " VALUES (?,?,?,?,?,?,?)");
  q.bind(1, static_cast<std::int64_t>(s.at_ns));
  q.bind(2, static_cast<std::int64_t>(s.balance));
  q.bind(3, static_cast<std::int64_t>(s.unrealized_pnl));
  q.bind(4, static_cast<std::int64_t>(s.isolated_margin));
  q.bind(5, static_cast<std::int64_t>(s.equity));
  q.bind(6, static_cast<std::int64_t>(s.high_water_mark));
  q.bind(7, s.reason);
  q.exec();
}

std::optional<Journal::AccountSnapshot> Journal::last_snapshot(const std::string& reason) {
  SQLite::Statement q(*db_,
                      "SELECT at_ns, balance_micro, unrealized_pnl_micro,"
                      " isolated_margin_micro, equity_micro, high_water_mark_micro, reason"
                      " FROM account_snapshots WHERE reason = ?"
                      " ORDER BY at_ns DESC LIMIT 1");
  q.bind(1, reason);
  if (!q.executeStep()) return std::nullopt;
  AccountSnapshot s;
  s.at_ns = q.getColumn(0).getInt64();
  s.balance = q.getColumn(1).getInt64();
  s.unrealized_pnl = q.getColumn(2).getInt64();
  s.isolated_margin = q.getColumn(3).getInt64();
  s.equity = q.getColumn(4).getInt64();
  s.high_water_mark = q.getColumn(5).getInt64();
  s.reason = q.getColumn(6).getString();
  return s;
}

std::vector<Journal::ReplayEvent> Journal::tail(std::size_t n) {
  std::vector<ReplayEvent> out;
  SQLite::Statement q(*db_,
                      "SELECT id, at_ns, kind, payload FROM journal_events"
                      " ORDER BY id DESC LIMIT ?");
  q.bind(1, static_cast<std::int64_t>(n));
  while (q.executeStep()) {
    out.push_back({q.getColumn(0).getInt64(),
                   q.getColumn(1).getInt64(),
                   q.getColumn(2).getString(),
                   q.getColumn(3).getString()});
  }
  return out;
}

void Journal::replay_since(core::Nanos since_ns,
                            std::function<void(const ReplayEvent&)> handler) {
  SQLite::Statement q(*db_,
                      "SELECT id, at_ns, kind, payload FROM journal_events"
                      " WHERE at_ns >= ? ORDER BY id ASC");
  q.bind(1, static_cast<std::int64_t>(since_ns));
  while (q.executeStep()) {
    ReplayEvent e{q.getColumn(0).getInt64(),
                  q.getColumn(1).getInt64(),
                  q.getColumn(2).getString(),
                  q.getColumn(3).getString()};
    handler(e);
  }
}

void Journal::put_strategy_state(const std::string& name,
                                 const std::vector<std::uint8_t>& blob,
                                 core::Nanos at_ns) {
  SQLite::Statement q(*db_,
                      "INSERT OR REPLACE INTO strategy_state(strategy_name, blob, updated_at_ns)"
                      " VALUES (?,?,?)");
  q.bind(1, name);
  q.bindNoCopy(2, blob.data(), static_cast<int>(blob.size()));
  q.bind(3, static_cast<std::int64_t>(at_ns));
  q.exec();
}

std::optional<std::vector<std::uint8_t>> Journal::get_strategy_state(
    const std::string& name) {
  SQLite::Statement q(*db_,
                      "SELECT blob FROM strategy_state WHERE strategy_name = ?");
  q.bind(1, name);
  if (!q.executeStep()) return std::nullopt;
  const auto col = q.getColumn(0);
  const auto* data = static_cast<const std::uint8_t*>(col.getBlob());
  const auto size = static_cast<std::size_t>(col.getBytes());
  return std::vector<std::uint8_t>(data, data + size);
}

}  // namespace propr::persist
