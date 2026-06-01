#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace propr::config {

struct ProfileEndpoints {
  std::string rest_base;
  std::string propr_ws;
  std::string hyperliquid_ws;
};

struct LimitsConfig {
  int normal_rate_per_min{800};
  int reserved_rate_per_min{400};
  int max_risk_per_trade_bps{200};
  int max_daily_headroom_use_bps{2500};
  int max_overall_headroom_use_bps{1000};
  int floating_loss_trip_bps{7000};
  int daily_loss_trip_bps{7000};
  int max_leverage_btc_eth{3};
  int max_leverage_other_crypto{2};
  int ws_blind_mode_after_ms{5000};
  int slippage_buffer_bps{20};
};

struct PathsConfig {
  std::string log_path{"logs/agent.jsonl"};
  std::string journal_path{"logs/journal.db"};
  std::string strategies_dir{"build/strategies"};
};

struct StrategyEntry {
  std::string name;
  std::string plugin_path;
  std::string params_path;
  int risk_budget_bps{10000};
  bool enabled{true};
};

struct RuntimeConfig {
  std::string profile{"beta"};
  std::unordered_map<std::string, ProfileEndpoints> profiles;
  LimitsConfig limits;
  PathsConfig paths;
  std::vector<StrategyEntry> strategies;
  std::string hmac_secret;
  std::string challenge_attempt_id;

  // Resolve the active profile's endpoints. Falls back to a sensible default
  // pointing at beta if the chosen profile is missing.
  const ProfileEndpoints& active() const;
};

// Loads the YAML file. If the env var PROPR_PROFILE is set, it overrides the
// `profile` field. If PROPR_HMAC_SECRET is set, it overrides hmac_secret.
RuntimeConfig load(const std::string& path);

}  // namespace propr::config
