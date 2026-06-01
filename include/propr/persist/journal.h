#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "propr/core/types.h"

namespace SQLite {
class Database;
}  // namespace SQLite

namespace propr::persist {

// Append-only journal in SQLite WAL mode. Carries every Decision, Trip, Reset, Order
// and Fill so we can reconstruct state on crash recovery.
class Journal {
 public:
  explicit Journal(const std::string& path);
  ~Journal();

  Journal(const Journal&) = delete;
  Journal& operator=(const Journal&) = delete;

  // Append a journal event. `kind` should be a short stable string like
  // "kill_switch_trip", "order_sent", "account_refetch".
  void write_event(core::Nanos at_ns, std::string_view kind, std::string_view json_payload);

  struct IntentRecord {
    std::string intent_uuid;
    std::string order_group_id;
    std::string intent_ids_json;
    std::string status;
    core::Nanos created_at_ns;
    std::optional<core::Nanos> resolved_at_ns;
  };

  // Write BEFORE the HTTP call. If the process crashes during the request, we restart
  // with the same intent IDs and Propr's server-side dedup handles the retry safely.
  void write_intent(const IntentRecord& r);
  void update_intent_status(const std::string& intent_uuid,
                            const std::string& status,
                            core::Nanos resolved_at_ns);

  std::vector<IntentRecord> unresolved_intents();

  struct AccountSnapshot {
    core::Nanos at_ns;
    core::Money balance{0};
    core::Money unrealized_pnl{0};
    core::Money isolated_margin{0};
    core::Money equity{0};
    core::Money high_water_mark{0};
    std::string reason;  // "daily_reset" | "kill_switch_trip" | "manual"
  };

  void write_snapshot(const AccountSnapshot& s);
  std::optional<AccountSnapshot> last_snapshot(const std::string& reason);

  // Replay the last N events of any kind, newest first. For diagnostics / startup banner.
  struct ReplayEvent {
    std::int64_t id;
    core::Nanos at_ns;
    std::string kind;
    std::string payload;
  };
  std::vector<ReplayEvent> tail(std::size_t n);

  // Chronological scan from `since_ns` (inclusive). Used by event-sourced replay
  // on process restart: the caller's handler interprets each kind/payload and
  // applies it to the in-memory state.
  void replay_since(core::Nanos since_ns,
                    std::function<void(const ReplayEvent&)> handler);

  // Strategy opaque-state blob persistence.
  void put_strategy_state(const std::string& name,
                          const std::vector<std::uint8_t>& blob,
                          core::Nanos at_ns);
  std::optional<std::vector<std::uint8_t>> get_strategy_state(const std::string& name);

 private:
  std::unique_ptr<SQLite::Database> db_;
};

}  // namespace propr::persist
