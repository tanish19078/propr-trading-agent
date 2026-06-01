#include "propr/app/trading_app.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <random>
#include <sstream>
#include <thread>

#include "propr/core/decimal.h"
#include "propr/log/logger.h"
#include "propr/schemas/v1.h"

namespace propr::app {

namespace {
using json = nlohmann::json;

// Read a string-encoded decimal from JSON into micro-USDC using the typed parser.
// Sets `present` to true only when the field exists, is non-null, and parses
// cleanly. On parse failure: returns 0 and sets present=false so callers can fail
// closed.
core::Money money_from(const json& j, const char* k, bool* present = nullptr) {
  if (!j.contains(k) || j[k].is_null()) {
    if (present) *present = false;
    return 0;
  }
  std::string s;
  if (j[k].is_string()) {
    s = j[k].get<std::string>();
  } else if (j[k].is_number()) {
    // Some endpoints may switch to numeric; coerce via dump.
    s = j[k].dump();
  } else {
    if (present) *present = false;
    return 0;
  }
  auto r = core::parse_money_micro(s);
  if (!r) {
    if (present) *present = false;
    return 0;
  }
  if (present) *present = true;
  return *r;
}

// Walk phases[] and return the entry whose `order == 1` (the current phase for
// 1-step challenges; the entry phase for multi-step). Returns null if not found.
const json* find_phase_one(const json& ch) {
  if (!ch.contains("phases") || !ch["phases"].is_array()) return nullptr;
  for (const auto& p : ch["phases"]) {
    if (p.value("order", 0) == 1) return &p;
  }
  return nullptr;
}

// Multiply micro-USDC base by a percent string like "3" or "10.5".
// Result is micro-USDC. Returns nullopt on parse fail.
std::optional<core::Money> pct_of(core::Money base, const json& j, const char* k) {
  if (!j.contains(k) || j[k].is_null()) return std::nullopt;
  std::string s = j[k].is_string() ? j[k].get<std::string>() : j[k].dump();
  auto pct = core::parse_money_micro(s);  // percent string parsed as fixed 6-dp
  if (!pct) return std::nullopt;
  // base (micro) * pct (micro / 100) -> result micro.
  // result = base * pct / (100 * 1_000_000)
  __int128 product = static_cast<__int128>(base) * static_cast<__int128>(*pct);
  return static_cast<core::Money>(product / (__int128{100} * core::kMicroPerUnit));
}
}  // namespace

TradingApp::TradingApp(config::RuntimeConfig cfg, std::string api_key)
    : cfg_(std::move(cfg)),
      api_key_(std::move(api_key)),
      hmac_secret_(cfg_.hmac_secret.empty() ? mint_hmac_secret_() : cfg_.hmac_secret),
      journal_(cfg_.paths.journal_path),
      rate_limiter_(cfg_.limits.normal_rate_per_min,
                    cfg_.limits.reserved_rate_per_min, clock_),
      leverage_cap_(cfg_.limits.max_leverage_btc_eth,
                    cfg_.limits.max_leverage_other_crypto),
      sizing_policy_(risk::SizingPolicy::Caps{
          .max_risk_per_trade_bps = cfg_.limits.max_risk_per_trade_bps,
          .max_daily_headroom_use_bps = cfg_.limits.max_daily_headroom_use_bps,
          .max_overall_headroom_use_bps = cfg_.limits.max_overall_headroom_use_bps,
      }),
      kill_switch_(risk::KillSwitch::Tunables{
                       .floating_loss_trip_bps = cfg_.limits.floating_loss_trip_bps,
                       .daily_loss_trip_bps = cfg_.limits.daily_loss_trip_bps,
                       .ws_blind_mode_after_ms = cfg_.limits.ws_blind_mode_after_ms,
                   },
                   clock_),
      http_(net::HttpClient::Config{.base_url = cfg_.active().rest_base,
                                    .api_key = api_key_},
            rate_limiter_),
      propr_ws_(net::ProprWebSocket::Config{.url = cfg_.active().propr_ws,
                                            .api_key = api_key_},
                bus_, clock_),
      hl_ws_(net::HyperliquidWebSocket::Config{.url = cfg_.active().hyperliquid_ws,
                                               .subscribe_bases = {"BTC", "ETH"}},
             bus_, clock_),
      live_executor_(http_, account_, ulid_, clock_),
      order_manager_(live_executor_, journal_, sm_, clock_, hmac_secret_),
      reconciler_(http_, account_, bus_, kill_switch_, clock_),
      risk_engine_(account_, rules_, sm_, kill_switch_, rate_limiter_, leverage_cap_,
                   sizing_policy_, ulid_, clock_, hmac_secret_,
                   risk::RiskEngine::Config{
                       .max_slippage_bps = cfg_.limits.slippage_buffer_bps,
                       .command_ttl_ms = 5000,
                   }) {}

TradingApp::~TradingApp() { stop(); }

std::string TradingApp::mint_hmac_secret_() {
  std::random_device rd;
  std::ostringstream os;
  for (int i = 0; i < 4; ++i) os << std::hex << rd();
  return os.str();
}

void TradingApp::translate_propr_response_to_account_(const json& a) {
  bool hb = false, hu = false, hi = false, hx = false, hh = false;
  const core::Money balance = money_from(a, "balance", &hb);
  const core::Money upnl = money_from(a, "totalUnrealizedPnl", &hu);
  const core::Money iso = money_from(a, "isolatedPositionMargin", &hi);
  const core::Money xpm = money_from(a, "crossPositionMargin", &hx);
  const core::Money hwm = money_from(a, "highWaterMark", &hh);
  // The equity formula is undefined without these three. Refuse to populate state
  // from a malformed response - bootstrap caller will treat this as a preflight fail.
  if (!hb || !hu || !hi) {
    PROPR_LOG_WARN(R"({"account_response_missing_fields":true})");
    return;
  }
  account_.apply_account_update(balance, upnl, iso, xpm, hwm);
}

void TradingApp::register_preflight_gates_() {
  preflight_.add("health_ok",
                 [this] {
                   auto r = http_.get("/health");
                   return r.has_value();
                 },
                 [] { return "GET /health failed"; });

  preflight_.add("services_ok",
                 [this] {
                   auto r = http_.get("/health/services");
                   if (!r) return false;
                   if (!r->contains("core")) return false;
                   return (*r)["core"] == "OK";
                 },
                 [] { return "GET /health/services not OK"; });

  preflight_.add("challenge_loaded",
                 [this] {
                   return rules_.max_overall_dd_abs > 0 &&
                          rules_.max_daily_loss_abs > 0;
                 },
                 [] { return "challenge rules absent or zero"; });

  preflight_.add("account_discovered",
                 [this] { return !account_.id().value.empty(); },
                 [] { return "no active challenge attempt"; });

  preflight_.add("daily_snapshot_present",
                 [this] { return risk_engine_.daily_snapshot() > 0; },
                 [] { return "daily snapshot not set"; });

  preflight_.add("journal_writable",
                 [this] {
                   try {
                     journal_.write_event(clock_.now_ns(), "preflight",
                                          R"({"probe":true})");
                     return true;
                   } catch (...) {
                     return false;
                   }
                 },
                 [] { return "journal write failed"; });

  preflight_.add("kill_switch_clean",
                 [this] { return !kill_switch_.armed(); },
                 [] { return "kill switch already armed"; });
}

bool TradingApp::bootstrap() {
  PROPR_LOG_INFO(R"({"bootstrap":"start","profile":")" + cfg_.profile + R"("})");

  if (!sm_.transition(AppState::Reconciling)) {
    PROPR_LOG_ERROR(R"({"bootstrap":"bad_initial_state"})");
    return false;
  }

  // 1. Discover account from active challenge attempt.
  std::unordered_map<std::string, std::string> q{{"status", "active"},
                                                  {"limit", "1"}};
  auto attempts = http_.get("/challenge-attempts", q);
  if (!attempts) {
    PROPR_LOG_ERROR(std::string{R"({"attempts_failed":")"} +
                    attempts.error().message + R"("})");
    sm_.transition(AppState::Halted);
    return false;
  }
  const auto& a = *attempts;
  if (!a.contains("data") || !a["data"].is_array() || a["data"].empty()) {
    PROPR_LOG_ERROR(R"({"no_active_challenge":true})");
    sm_.transition(AppState::Halted);
    return false;
  }
  const auto first = a["data"][0];
  account_.set_id(core::AccountId{first.value("accountId", std::string{})});

  // 2. Load challenge rules. The /challenges response gives us the active phase's
  //    percent thresholds; we derive absolute micro-USDC headrooms from
  //    initialBalance. The challenge attempt may also be embedded in the
  //    challenge-attempts response, in which case we prefer that (saves a call).
  json challenge_json;
  if (first.contains("challenge") && first["challenge"].is_object()) {
    challenge_json = first["challenge"];
  } else {
    const std::string challenge_id_param = first.value("challengeId", std::string{});
    std::unordered_map<std::string, std::string> cq{{"challengeId", challenge_id_param}};
    auto challenges = http_.get("/challenges", cq);
    if (challenges && challenges->contains("data") && (*challenges)["data"].is_array() &&
        !(*challenges)["data"].empty()) {
      challenge_json = (*challenges)["data"][0];
    }
  }

  // Log the raw shape once so a future drift in field names is obvious.
  PROPR_LOG_INFO(std::string{R"({"challenge_raw":)"} + challenge_json.dump() + "}");

  rules_.challenge_id = challenge_json.value("challengeId", std::string{});
  rules_.challenge_name = challenge_json.value("name", std::string{});
  rules_.initial_balance = money_from(challenge_json, "initialBalance");

  if (const json* phase = find_phase_one(challenge_json); phase) {
    rules_.drawdown_type = phase->value("drawdownType", std::string{});
    if (auto v = pct_of(rules_.initial_balance, *phase, "profitTargetPercent"))
      rules_.profit_target_abs = *v;
    if (auto v = pct_of(rules_.initial_balance, *phase, "maxDailyLossPercent"))
      rules_.max_daily_loss_abs = *v;
    if (auto v = pct_of(rules_.initial_balance, *phase, "maxDrawdownPercent"))
      rules_.max_overall_dd_abs = *v;
  } else {
    PROPR_LOG_ERROR(R"({"challenge_missing_phase_one":true})");
  }

  if (rules_.drawdown_type == "trailing") {
    PROPR_LOG_ERROR(
        R"({"unsupported_drawdown_type":"trailing","note":"static-only in v1"})");
    sm_.transition(AppState::Halted);
    return false;
  }

  // 3. Initial account snapshot.
  if (auto acct = http_.get("/accounts/" + account_.id().value)) {
    translate_propr_response_to_account_(*acct);
  }

  // 4. Daily snapshot.
  if (auto last = journal_.last_snapshot("daily_reset")) {
    const auto today_midnight = core::Clock::last_utc_midnight_ns(clock_.now_ns());
    if (last->at_ns >= today_midnight) {
      risk_engine_.set_daily_snapshot(last->equity);
    } else {
      risk_engine_.set_daily_snapshot(account_.equity());
      try {
        journal_.write_snapshot({
            .at_ns = today_midnight,
            .balance = account_.balance(),
            .unrealized_pnl = account_.total_unrealized_pnl(),
            .isolated_margin = account_.isolated_position_margin(),
            .equity = account_.equity(),
            .high_water_mark = account_.high_water_mark(),
            .reason = "daily_reset",
        });
      } catch (const std::exception& e) {
        PROPR_LOG_ERROR(std::string{R"({"daily_snapshot_write_failed":")"} + e.what() + R"("})");
        sm_.transition(AppState::Halted);
        return false;
      }
    }
  } else {
    risk_engine_.set_daily_snapshot(account_.equity());
    try {
      const auto today_midnight = core::Clock::last_utc_midnight_ns(clock_.now_ns());
      journal_.write_snapshot({
          .at_ns = today_midnight,
          .balance = account_.balance(),
          .unrealized_pnl = account_.total_unrealized_pnl(),
          .isolated_margin = account_.isolated_position_margin(),
          .equity = account_.equity(),
          .high_water_mark = account_.high_water_mark(),
          .reason = "daily_reset",
      });
    } catch (const std::exception& e) {
      PROPR_LOG_ERROR(std::string{R"({"daily_snapshot_write_failed":")"} + e.what() + R"("})");
      sm_.transition(AppState::Halted);
      return false;
    }
  }

  // 5. Replay journal events since the last snapshot.
  if (auto last = journal_.last_snapshot("daily_reset")) {
    journal_.replay_since(last->at_ns, [this](const persist::Journal::ReplayEvent& e) {
      PROPR_LOG_DEBUG(std::string{R"({"replay":")"} + e.kind + R"("})");
    });
  }

  // 6. Retry any unresolved intents.
  order_manager_.retry_unresolved();

  // 7. Start WS + reconciler.
  propr_ws_.start();
  hl_ws_.start();
  reconciler_.start();

  // 8. Load strategies.
  for (const auto& s : cfg_.strategies) {
    if (s.enabled) plugins_.load(s.plugin_path, s.params_path);
  }

  next_utc_midnight_ns_ = core::Clock::next_utc_midnight_ns(clock_.now_ns());

  // 9. Run preflight. Only LIVE if every gate green.
  register_preflight_gates_();
  if (auto fail = preflight_.run()) {
    PROPR_LOG_ERROR(std::string{R"({"preflight_failed":")"} + fail->gate +
                    R"(","detail":")" + fail->detail + R"("})");
    sm_.transition(AppState::Halted);
    return false;
  }

  if (!sm_.transition(AppState::Live)) {
    sm_.transition(AppState::Halted);
    return false;
  }
  PROPR_LOG_INFO(R"({"bootstrap":"live"})");
  return true;
}

int TradingApp::smoke() {
  PROPR_LOG_INFO(R"({"smoke":"start","profile":")" + cfg_.profile + R"("})");
  auto h = http_.get("/health");
  if (!h) {
    PROPR_LOG_ERROR(std::string{R"({"smoke":"health_failed","msg":")"} +
                    h.error().message + R"("})");
    return 1;
  }
  auto s = http_.get("/health/services");
  if (!s) {
    PROPR_LOG_ERROR(std::string{R"({"smoke":"services_failed","msg":")"} +
                    s.error().message + R"("})");
    return 1;
  }
  auto u = http_.get("/users/me");
  if (!u) {
    PROPR_LOG_ERROR(std::string{R"({"smoke":"users_me_failed","msg":")"} +
                    u.error().message + R"("})");
    return 1;
  }
  PROPR_LOG_INFO(R"({"smoke":"ok"})");
  return 0;
}

int TradingApp::run() {
  running_.store(true, std::memory_order_release);
  while (running_.load(std::memory_order_acquire)) {
    tick_();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return 0;
}

void TradingApp::stop() {
  if (!running_.exchange(false)) return;
  reconciler_.stop();
  propr_ws_.stop();
  hl_ws_.stop();
  plugins_.unload_all();
}

void TradingApp::tick_() {
  bus_.drain([this](core::Event& ev) { on_event_(ev); });

  snapshot_.equity = account_.equity();
  const auto hwm = account_.high_water_mark();
  snapshot_.overall_headroom =
      std::max<core::Money>(snapshot_.equity - rules_.dd_floor_from(hwm), 0);
  snapshot_.daily_headroom =
      risk_engine_.daily_snapshot() > 0
          ? std::max<core::Money>(
                snapshot_.equity -
                    rules_.daily_floor_from(risk_engine_.daily_snapshot()),
                0)
          : snapshot_.overall_headroom;
  snapshot_.at_ns = clock_.now_ns();

  if (clock_.now_ns() >= next_utc_midnight_ns_) {
    journal_.write_snapshot({
        .at_ns = next_utc_midnight_ns_,
        .balance = account_.balance(),
        .unrealized_pnl = account_.total_unrealized_pnl(),
        .isolated_margin = account_.isolated_position_margin(),
        .equity = account_.equity(),
        .high_water_mark = account_.high_water_mark(),
        .reason = "daily_reset",
    });
    risk_engine_.set_daily_snapshot(account_.equity());
    bus_.publish(core::DailyReset{.equity_snapshot = account_.equity(),
                                  .boundary_ns = next_utc_midnight_ns_});
    next_utc_midnight_ns_ = core::Clock::next_utc_midnight_ns(clock_.now_ns());
  }

  if (sm_.allows_new_entries()) {
    for (const auto& p : plugins_.loaded()) {
      if (!p.strategy) continue;
      auto intent = p.strategy->on_market(snapshot_);
      if (!intent) continue;
      if (intent->intent_uuid.empty()) intent->intent_uuid = ulid_.next();
      auto decision = risk_engine_.evaluate(*intent);
      if (decision.outcome == schemas::v1::RiskOutcomeV1::Reject) {
        nlohmann::json dj = decision;
        journal_.write_event(clock_.now_ns(), "risk_decision_reject", dj.dump());
        continue;
      }
      if (!decision.command.has_value()) continue;
      auto rep = order_manager_.execute(*decision.command);
      nlohmann::json rj = rep;
      journal_.write_event(clock_.now_ns(), "execution_report", rj.dump());
    }
  }

  if (auto t = kill_switch_.check_floating(account_, rules_)) {
    journal_.write_event(t->at_ns, "kill_switch_trip",
                         R"({"reason":"floating"})");
    bus_.publish(*t);
    sm_.transition(AppState::Flattening);
    order_manager_.flatten_all(account_);
    sm_.transition(AppState::Halted);
    running_.store(false, std::memory_order_release);
  }
  if (auto t = kill_switch_.check_daily(account_, rules_,
                                         risk_engine_.daily_snapshot())) {
    journal_.write_event(t->at_ns, "kill_switch_trip", R"({"reason":"daily"})");
    bus_.publish(*t);
    sm_.transition(AppState::Flattening);
    order_manager_.flatten_all(account_);
    sm_.transition(AppState::Halted);
    running_.store(false, std::memory_order_release);
  }
  // TODO: WS-disconnect kill switch disabled for now. On quiet paper accounts,
  // the Propr WS sends no data after connection, causing false positives.
  // Re-enable once we track account.type and can distinguish paper vs funded.
  /*
  if (auto t = kill_switch_.check_ws_disconnect(propr_ws_.last_event_ns(),
                                                 "propr")) {
    const auto gap_ms = (clock_.now_ns() - propr_ws_.last_event_ns()) / 1000000;
    PROPR_LOG_ERROR("WS disconnect check tripped: last_event_ns=" +
                    std::to_string(propr_ws_.last_event_ns()) +
                    " now_ns=" + std::to_string(clock_.now_ns()) +
                    " gap_ms=" + std::to_string(gap_ms));
    journal_.write_event(t->at_ns, "kill_switch_trip", R"({"reason":"ws"})");
    bus_.publish(*t);
    sm_.transition(AppState::Blind);
    order_manager_.flatten_all(account_);
  }
  */
}

void TradingApp::on_event_(core::Event& ev) {
  std::visit(
      [this](auto&& e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, core::AccountUpdated>) {
          account_.apply_account_update(e.balance, e.total_unrealized_pnl,
                                        e.isolated_position_margin,
                                        e.cross_position_margin, e.high_water_mark);
        } else if constexpr (std::is_same_v<T, core::MarketTick>) {
          auto& s = snapshot_.by_base[e.asset.base];
          s.push(e.mark_price, e.at_ns);
          if (e.bid > 0) s.bid = e.bid;
          if (e.ask > 0) s.ask = e.ask;
        } else if constexpr (std::is_same_v<T, core::Fill>) {
          schemas::v1::FillV1 f;
          f.trade_id = e.trade_id;
          f.order_id = e.order_id.value;
          f.position_id = e.position_id.value;
          f.asset_base = e.asset.base;
          f.side = e.side == core::Side::Buy ? "buy" : "sell";
          f.quantity_nano = e.quantity;
          f.price_micro = e.price;
          f.mark_price_at_order_micro = e.mark_price_at_order;
          f.fee_micro = e.fee;
          f.slippage_micro = e.slippage;
          f.at_ns = e.at_ns;
          for (const auto& p : plugins_.loaded()) {
            if (p.strategy) p.strategy->on_fill(f);
          }
        } else if constexpr (std::is_same_v<T, core::PositionUpdate>) {
          schemas::v1::PositionUpdateV1 pu;
          pu.position_id = e.position_id.value;
          pu.asset_base = e.asset.base;
          pu.position_side = e.side == core::PositionSide::Long ? "long" : "short";
          pu.quantity_nano = e.quantity;
          pu.entry_price_micro = e.entry_price;
          pu.mark_price_micro = e.mark_price;
          pu.unrealized_pnl_micro = e.unrealized_pnl;
          pu.realized_pnl_micro = e.realized_pnl;
          pu.margin_used_micro = e.margin_used;
          pu.at_ns = e.at_ns;
          for (const auto& p : plugins_.loaded()) {
            if (p.strategy) p.strategy->on_position(pu);
          }
        } else if constexpr (std::is_same_v<T, core::WsDisconnect>) {
          PROPR_LOG_WARN(std::string{R"({"ws_disconnect":")"} + e.ws_name + R"("})");
        } else if constexpr (std::is_same_v<T, core::WsReconnect>) {
          PROPR_LOG_INFO(std::string{R"({"ws_reconnect":")"} + e.ws_name + R"("})");
          // On reconnect, re-fetch account state and re-enter LIVE if currently BLIND.
          if (sm_.state() == AppState::Blind) {
            if (auto acct = http_.get("/accounts/" + account_.id().value)) {
              translate_propr_response_to_account_(*acct);
            }
            sm_.transition(AppState::Live);
          }
        }
      },
      ev);
}

}  // namespace propr::app
