#include <gtest/gtest.h>

#include <set>
#include <thread>

#include "propr/core/ulid.h"

using propr::core::Ulid;

TEST(UlidTest, IsTwentySixCharsAndCrockfordOnly) {
  Ulid u;
  const std::string s = u.next();
  EXPECT_EQ(s.size(), 26u);
  for (char c : s) {
    const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z' && c != 'I' &&
                                                c != 'L' && c != 'O' && c != 'U');
    EXPECT_TRUE(ok) << "bad char '" << c << "'";
  }
}

TEST(UlidTest, MonotonicWithinSameMs) {
  Ulid u(/*seed=*/123);
  std::vector<std::string> ids;
  for (int i = 0; i < 1000; ++i) ids.push_back(u.next());
  for (std::size_t i = 1; i < ids.size(); ++i) {
    EXPECT_LT(ids[i - 1], ids[i]) << "ids[" << i - 1 << "]=" << ids[i - 1]
                                   << " >= ids[" << i << "]=" << ids[i];
  }
}

TEST(UlidTest, ThreadSafeUnique) {
  Ulid u;
  std::set<std::string> seen;
  std::mutex mu;
  std::vector<std::thread> ts;
  for (int t = 0; t < 8; ++t) {
    ts.emplace_back([&]() {
      for (int i = 0; i < 1000; ++i) {
        auto s = u.next();
        std::lock_guard<std::mutex> g(mu);
        ASSERT_TRUE(seen.insert(s).second) << "duplicate ULID " << s;
      }
    });
  }
  for (auto& t : ts) t.join();
  EXPECT_EQ(seen.size(), 8u * 1000u);
}
