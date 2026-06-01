#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "propr/persist/journal.h"

namespace fs = std::filesystem;
using propr::persist::Journal;
using propr::core::Money;

namespace {
std::string tmp_db() {
  auto p = fs::temp_directory_path() / ("propr_test_" + std::to_string(std::rand()) + ".db");
  return p.string();
}
}  // namespace

TEST(JournalTest, EventsRoundtripAndTailReturnsRecent) {
  const std::string path = tmp_db();
  {
    Journal j(path);
    j.write_event(100, "kind_a", "{\"x\":1}");
    j.write_event(200, "kind_b", "{\"x\":2}");
    j.write_event(300, "kind_a", "{\"x\":3}");
  }
  {
    Journal j(path);
    auto t = j.tail(2);
    ASSERT_EQ(t.size(), 2u);
    EXPECT_EQ(t[0].at_ns, 300);
    EXPECT_EQ(t[1].at_ns, 200);
  }
  fs::remove(path);
}

TEST(JournalTest, IntentsAreIdempotentByUuid) {
  const std::string path = tmp_db();
  {
    Journal j(path);

    Journal::IntentRecord r1{
        .intent_uuid = "abc",
        .order_group_id = "GROUP1",
        .intent_ids_json = "{\"entry\":\"E1\"}",
        .status = "pending",
        .created_at_ns = 100,
        .resolved_at_ns = std::nullopt,
    };
    j.write_intent(r1);
    // Re-write with same UUID — must replace, not insert duplicate.
    j.write_intent(r1);

    auto open = j.unresolved_intents();
    ASSERT_EQ(open.size(), 1u);
    EXPECT_EQ(open[0].intent_uuid, "abc");
    EXPECT_EQ(open[0].order_group_id, "GROUP1");

    j.update_intent_status("abc", "sent", 200);
    EXPECT_TRUE(j.unresolved_intents().empty());
  }
  std::error_code ec; fs::remove(path, ec);
}

TEST(JournalTest, SnapshotRoundtrip) {
  const std::string path = tmp_db();
  {
    Journal j(path);
    Journal::AccountSnapshot s{
        .at_ns = 500,
        .balance = 1000,
        .unrealized_pnl = 50,
        .isolated_margin = 0,
        .equity = 1050,
        .high_water_mark = 1100,
        .reason = "daily_reset",
    };
    j.write_snapshot(s);
    auto last = j.last_snapshot("daily_reset");
    ASSERT_TRUE(last.has_value());
    EXPECT_EQ(last->equity, 1050);
    EXPECT_EQ(last->high_water_mark, 1100);
  }
  std::error_code ec; fs::remove(path, ec);
}
