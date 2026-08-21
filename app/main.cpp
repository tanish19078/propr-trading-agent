#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

#include "propr/app/sim_runner.h"
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
      "                   [--sim [--ticks N] [--seed S]\n"
      "                         [--strategy PATH] [--params PATH]]\n"
      "\n"
      "  --config PATH    YAML config file (default: config/runtime.yaml)\n"
      "  --profile NAME   Override the profile from config (default: beta)\n"
      "  --smoke          Run health checks + GET /users/me then exit\n"
      "  --sim            Offline simulator session; no REST/WS/API key\n"
      "  --ticks N        Sim ticks (default 2000; 30s of logical time each)\n"
      "  --seed S         Sim seed for the price walk and simulator RNG\n"
      "  --strategy PATH  Plugin path override (default: first enabled in config)\n"
      "  --params PATH    Strategy params override\n"
      "\n"
      "  PROPR_API_KEY    Required unless --sim; pk_beta_... demo, pk_live_... live\n"
      "  PROPR_PROFILE    Overrides config and --profile\n"
      "  PROPR_HMAC_SECRET  Optional; uses a random per-session secret if unset\n";
}
}  // namespace

int main(int argc, char** argv) {
  std::string config_path = "config/runtime.yaml";
  std::string profile_override;
  std::string strategy_override, params_override;
  bool smoke = false;
  bool sim = false;
  int sim_ticks = 2000;
  std::uint64_t sim_seed = 42;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--config" && i + 1 < argc) {
      config_path = argv[++i];
    } else if (a == "--profile" && i + 1 < argc) {
      profile_override = argv[++i];
    } else if (a == "--smoke") {
      smoke = true;
    } else if (a == "--sim") {
      sim = true;
    } else if (a == "--ticks" && i + 1 < argc) {
      sim_ticks = std::stoi(argv[++i]);
    } else if (a == "--seed" && i + 1 < argc) {
      sim_seed = std::stoull(argv[++i]);
    } else if (a == "--strategy" && i + 1 < argc) {
      strategy_override = argv[++i];
    } else if (a == "--params" && i + 1 < argc) {
      params_override = argv[++i];
    } else if (a == "--help" || a == "-h") {
      usage();
      return 0;
    }
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

  if (sim) {
    propr::app::SimRunner::Config rc{.ticks = sim_ticks,
                                     .seed = sim_seed,
                                     .strategy_path = strategy_override,
                                     .params_path = params_override};
    propr::app::SimRunner runner(std::move(cfg), rc);
    return runner.run();
  }

  const char* env_key = std::getenv("PROPR_API_KEY");
  if (!env_key || std::string(env_key).empty()) {
    std::cerr << "PROPR_API_KEY not set. Generate one at app.beta.propr.xyz "
                 "(demo) or app.propr.xyz (live), put it in .env, or use "
                 "--sim to trade the offline simulator.\n";
    return 2;
  }

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
