#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

#include "propr/app/trading_app.h"
#include "propr/config/config.h"
#include "propr/log/logger.h"

namespace {
propr::app::TradingApp* g_app = nullptr;

void on_signal(int) {
  if (g_app) g_app->stop();
}

void usage() {
  std::cout <<
      "usage: propr_agent [--config PATH] [--profile beta|live] [--smoke]\n"
      "\n"
      "  --config PATH    YAML config file (default: config/runtime.yaml)\n"
      "  --profile NAME   Override the profile from config (default: beta)\n"
      "  --smoke          Run health checks + GET /users/me then exit\n"
      "\n"
      "  PROPR_API_KEY    Required env var; pk_beta_... for demo or pk_live_... for live\n"
      "  PROPR_PROFILE    Overrides config and --profile\n"
      "  PROPR_HMAC_SECRET  Optional; uses a random per-session secret if unset\n";
}
}  // namespace

int main(int argc, char** argv) {
  std::string config_path = "config/runtime.yaml";
  std::string profile_override;
  bool smoke = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--config" && i + 1 < argc) {
      config_path = argv[++i];
    } else if (a == "--profile" && i + 1 < argc) {
      profile_override = argv[++i];
    } else if (a == "--smoke") {
      smoke = true;
    } else if (a == "--help" || a == "-h") {
      usage();
      return 0;
    }
  }

  const char* env_key = std::getenv("PROPR_API_KEY");
  if (!env_key || std::string(env_key).empty()) {
    std::cerr << "PROPR_API_KEY not set. Generate one at app.beta.propr.xyz "
                 "(demo) or app.propr.xyz (live) and put it in .env.\n";
    return 2;
  }

  propr::config::RuntimeConfig cfg;
  try {
    cfg = propr::config::load(config_path);
  } catch (const std::exception& e) {
    std::cerr << "config error: " << e.what() << "\n";
    return 2;
  }
  if (!profile_override.empty()) cfg.profile = profile_override;

  propr::log::init(cfg.paths.log_path);

  propr::app::TradingApp app(std::move(cfg), env_key);
  g_app = &app;
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  if (smoke) {
    return app.smoke();
  }
  if (!app.bootstrap()) {
    std::cerr << "bootstrap failed; see logs/agent.jsonl for details\n";
    return 1;
  }
  return app.run();
}
