// test_strategy_sim - score every strategy plugin across every synthetic regime
// through the real risk core, and print a comparison table.
//
// Plugin paths and params dirs are injected by CMake as compile definitions
// (RANGE_MR_PLUGIN, MOMENTUM_PLUGIN, ...). Each strategy is loaded as its real
// built .so/.dll, so this also smoke-tests the plugin ABI end to end.
//
// The hard assertion is the survival bar from CLAUDE.md ("survival > peak P&L"):
// no strategy may let the kill switch flatten the account in ANY regime. P&L
// itself is reported, not asserted — markets decide that, the test just measures.

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "integration/sim_harness.h"

namespace fs = std::filesystem;
using namespace propr::simharness;

namespace {

struct StratSpec {
  std::string plugin;
  std::string params;
};

std::vector<StratSpec> all_strategies() {
  return {
      {RANGE_MR_PLUGIN, RANGE_MR_PARAMS},
      {MOMENTUM_PLUGIN, MOMENTUM_PARAMS},
      {DONCHIAN_PLUGIN, DONCHIAN_PARAMS},
      {MA_CROSS_PLUGIN, MA_CROSS_PARAMS},
  };
}

std::vector<Regime> all_regimes() {
  return {Regime::Uptrend, Regime::Downtrend, Regime::Range, Regime::HighVolChop};
}

std::string money(propr::core::Money m) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%+.2f", double(m) / 1'000'000.0);
  return buf;
}

}  // namespace

TEST(StrategySim, CompareAcrossRegimes) {
  const std::string db = (fs::temp_directory_path() / "strategy_sim.db").string();

  std::vector<SimResult> results;
  for (const auto& s : all_strategies()) {
    for (Regime r : all_regimes()) {
      { std::error_code e; fs::remove(db, e); }
      // Same seed per regime so every strategy faces the identical tape.
      const std::uint64_t seed = 0xA11CE5ULL + static_cast<std::uint64_t>(r) * 1000;
      auto ticks = make_regime(r, seed);
      auto res = run_strategy_sim(s.plugin, s.params, ticks, regime_name(r), db);
      EXPECT_NE(res.strategy, "<load-failed>")
          << "could not load plugin: " << s.plugin;
      results.push_back(res);
    }
  }
  { std::error_code e; fs::remove(db, e); }

  // ── Comparison table ──────────────────────────────────────────────────────
  std::printf("\n");
  std::printf("%-18s %-13s %12s %12s %8s %8s %9s\n", "strategy", "regime",
              "net_pnl", "max_dd", "trades", "win%", "survived");
  std::printf("%s\n", std::string(86, '-').c_str());
  for (const auto& r : results) {
    std::printf("%-18s %-13s %12s %12s %8d %7.0f%% %9s\n", r.strategy.c_str(),
                r.regime.c_str(), money(r.net_pnl).c_str(),
                money(r.max_drawdown).c_str(), r.trades, r.win_rate() * 100.0,
                r.survived ? "yes" : "NO");
  }
  std::printf("%s\n", std::string(86, '-').c_str());

  // ── The survival bar: no strategy may blow the account in any regime ───────
  for (const auto& r : results) {
    EXPECT_TRUE(r.survived)
        << r.strategy << " let the kill switch flatten the account in regime "
        << r.regime << " (net_pnl=" << money(r.net_pnl) << ")";
  }
}
