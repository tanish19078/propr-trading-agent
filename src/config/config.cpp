#include "propr/config/config.h"

#include <yaml-cpp/yaml.h>

#include <cstdlib>
#include <stdexcept>

namespace propr::config {

namespace {
template <typename T>
T get_or(const YAML::Node& n, const char* key, T def) {
  if (!n || !n[key] || n[key].IsNull()) return def;
  return n[key].as<T>();
}
}  // namespace

const ProfileEndpoints& RuntimeConfig::active() const {
  auto it = profiles.find(profile);
  if (it != profiles.end()) return it->second;
  it = profiles.find("beta");
  if (it != profiles.end()) return it->second;
  static const ProfileEndpoints fallback{
      "https://api.beta.propr.xyz/v1",
      "wss://api.beta.propr.xyz/ws",
      "wss://api.hyperliquid.xyz/ws",
  };
  return fallback;
}

RuntimeConfig load(const std::string& path) {
  YAML::Node root = YAML::LoadFile(path);
  RuntimeConfig cfg;

  cfg.profile = get_or<std::string>(root, "profile", "beta");
  if (const char* env = std::getenv("PROPR_PROFILE"); env && *env) {
    cfg.profile = env;
  }

  if (auto ps = root["profiles"]) {
    for (auto it = ps.begin(); it != ps.end(); ++it) {
      const std::string name = it->first.as<std::string>();
      const auto& v = it->second;
      ProfileEndpoints e;
      e.rest_base = get_or<std::string>(v, "rest_base", "");
      e.propr_ws = get_or<std::string>(v, "propr_ws", "");
      e.hyperliquid_ws = get_or<std::string>(v, "hyperliquid_ws", "");
      cfg.profiles[name] = e;
    }
  }

  if (auto lim = root["limits"]) {
    cfg.limits.normal_rate_per_min =
        get_or<int>(lim, "normal_rate_per_min", cfg.limits.normal_rate_per_min);
    cfg.limits.reserved_rate_per_min =
        get_or<int>(lim, "reserved_rate_per_min", cfg.limits.reserved_rate_per_min);
    cfg.limits.max_risk_per_trade_bps =
        get_or<int>(lim, "max_risk_per_trade_bps", cfg.limits.max_risk_per_trade_bps);
    cfg.limits.max_daily_headroom_use_bps = get_or<int>(
        lim, "max_daily_headroom_use_bps", cfg.limits.max_daily_headroom_use_bps);
    cfg.limits.max_overall_headroom_use_bps = get_or<int>(
        lim, "max_overall_headroom_use_bps", cfg.limits.max_overall_headroom_use_bps);
    cfg.limits.floating_loss_trip_bps = get_or<int>(
        lim, "floating_loss_trip_bps", cfg.limits.floating_loss_trip_bps);
    cfg.limits.daily_loss_trip_bps =
        get_or<int>(lim, "daily_loss_trip_bps", cfg.limits.daily_loss_trip_bps);
    cfg.limits.max_leverage_btc_eth =
        get_or<int>(lim, "max_leverage_btc_eth", cfg.limits.max_leverage_btc_eth);
    cfg.limits.max_leverage_other_crypto = get_or<int>(
        lim, "max_leverage_other_crypto", cfg.limits.max_leverage_other_crypto);
    cfg.limits.ws_blind_mode_after_ms = get_or<int>(
        lim, "ws_blind_mode_after_ms", cfg.limits.ws_blind_mode_after_ms);
    cfg.limits.slippage_buffer_bps =
        get_or<int>(lim, "slippage_buffer_bps", cfg.limits.slippage_buffer_bps);
  }

  if (auto p = root["paths"]) {
    cfg.paths.log_path = get_or<std::string>(p, "log_path", cfg.paths.log_path);
    cfg.paths.journal_path = get_or<std::string>(p, "journal_path", cfg.paths.journal_path);
    cfg.paths.strategies_dir =
        get_or<std::string>(p, "strategies_dir", cfg.paths.strategies_dir);
  }

  if (auto s = root["strategies"]; s && s.IsSequence()) {
    for (const auto& entry : s) {
      StrategyEntry e;
      e.name = get_or<std::string>(entry, "name", "");
      e.plugin_path = get_or<std::string>(entry, "plugin_path", "");
      e.params_path = get_or<std::string>(entry, "params_path", "");
      e.risk_budget_bps = get_or<int>(entry, "risk_budget_bps", e.risk_budget_bps);
      e.enabled = get_or<bool>(entry, "enabled", true);
      if (e.name.empty() || e.plugin_path.empty()) {
        throw std::runtime_error("strategy entry missing name or plugin_path");
      }
      cfg.strategies.push_back(std::move(e));
    }
  }

  cfg.hmac_secret = get_or<std::string>(root, "hmac_secret", std::string{});
  if (const char* env = std::getenv("PROPR_HMAC_SECRET"); env && *env) {
    cfg.hmac_secret = env;
  }
  cfg.challenge_attempt_id =
      get_or<std::string>(root, "challenge_attempt_id", std::string{});

  return cfg;
}

}  // namespace propr::config
