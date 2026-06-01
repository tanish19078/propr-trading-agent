#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "propr/account/account.h"
#include "propr/account/challenge_rules.h"
#include "propr/app/preflight.h"
#include "propr/app/state_machine.h"
#include "propr/config/config.h"
#include "propr/core/clock.h"
#include "propr/core/event_bus.h"
#include "propr/core/ulid.h"
#include "propr/exec/order_executor.h"
#include "propr/exec/order_manager.h"
#include "propr/exec/propre_http_executor.h"
#include "propr/exec/reconciler.h"
#include "propr/net/http_client.h"
#include "propr/net/hyperliquid_ws.h"
#include "propr/net/propr_ws.h"
#include "propr/persist/journal.h"
#include "propr/risk/kill_switch.h"
#include "propr/risk/leverage_cap.h"
#include "propr/risk/rate_limiter.h"
#include "propr/risk/risk_engine.h"
#include "propr/risk/sizing_policy.h"
#include "propr/strategy/market_snapshot.h"
#include "propr/strategy/plugin_loader.h"

namespace propr::app {

class TradingApp {
 public:
  TradingApp(config::RuntimeConfig cfg, std::string api_key);
  ~TradingApp();

  // One-shot bootstrap. Walks every preflight gate; transitions
  // STARTING -> RECONCILING -> LIVE only if all gates green. Otherwise HALTED.
  bool bootstrap();

  // Blocking main loop. Returns when stop() is called or fatal error occurs.
  int run();

  // Smoke test mode: bootstrap, log the result, exit. Used by `make smoke-net`.
  int smoke();

  void stop();

  // Inspection (used by tests / tools).
  account::Account& account() { return account_; }
  risk::KillSwitch& kill_switch() { return kill_switch_; }
  risk::RiskEngine& risk_engine() { return risk_engine_; }
  StateMachine& state_machine() { return sm_; }
  strategy::MarketSnapshot& snapshot() { return snapshot_; }

 private:
  void tick_();
  void on_event_(core::Event& ev);
  void register_preflight_gates_();
  std::string mint_hmac_secret_();
  void translate_propr_response_to_account_(const nlohmann::json& acct_response);

  config::RuntimeConfig cfg_;
  std::string api_key_;
  std::string hmac_secret_;

  core::SystemClock clock_;
  core::Ulid ulid_;
  core::EventBus bus_;

  persist::Journal journal_;
  account::Account account_;
  account::ChallengeRules rules_;

  StateMachine sm_;
  Preflight preflight_;

  risk::RateLimiter rate_limiter_;
  risk::LeverageCap leverage_cap_;
  risk::SizingPolicy sizing_policy_;
  risk::KillSwitch kill_switch_;

  net::HttpClient http_;
  net::ProprWebSocket propr_ws_;
  net::HyperliquidWebSocket hl_ws_;

  exec::PropreHttpExecutor live_executor_;
  exec::OrderManager order_manager_;
  exec::Reconciler reconciler_;

  risk::RiskEngine risk_engine_;

  strategy::PluginLoader plugins_;
  strategy::MarketSnapshot snapshot_;

  std::atomic<bool> running_{false};
  std::int64_t next_utc_midnight_ns_{0};
};

}  // namespace propr::app
