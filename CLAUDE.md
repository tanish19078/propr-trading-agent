# Propr Agent

Pure C++ trading agent targeting a Propr 1-Step challenge. Read this file before
changing anything: the rules below are load-bearing invariants, not style notes.

## Mission

Pass a Propr 1-Step challenge ($60-$110 tier, 5K-10K account), get funded, and run
a strategy that survives the drawdown rules long enough to compound payouts.
Survival > peak P&L. A funded account that lives forever beats a blown account
with a great equity curve.

The product is the safety state machine. The strategy is interchangeable.

## Operating reality (Propr platform, verified from XBorgLabs/propr-docs)

### Endpoints and auth
- REST base: `https://api.propr.xyz/v1`. WS: `wss://api.propr.xyz/ws`.
- Auth header: `X-API-Key: pk_live_...` (one key per user).
- Health gates before every session: `GET /health` and `GET /health/services`.

### Challenge rules: fetch, do not hardcode
No numeric thresholds live in code. The challenge rules (profit target, max
drawdown, max daily loss, min trading days) are per-challenge and returned by
`GET /challenges`. The app refuses to enter LIVE state if that call has not
returned and populated `ChallengeRules`.

Account ID is auto-discovered:
`GET /challenge-attempts?status=active -> data[0].accountId`.

Failure reasons (only three): `max_drawdown_exceeded`, `max_daily_loss_exceeded`,
`profit_target_not_met`. The risk engine is designed to make the first two
impossible by construction.

### Equity formula (the most important line in the project)
```
equity = balance + total_unrealized_pnl + isolated_position_margin
```
Computed client-side from `GET /accounts/{accountId}`. There is no `equity` field
on the API. Every drawdown check, every kill switch, every position-sizing
decision uses this formula. Implemented exactly once, in
`src/account/account.cpp::Account::equity()`.

### Drawdown and daily loss
- Both are equity-based and enforced server-side. Floating P&L counts.
- Daily reset at 00:00 UTC. The C++ core schedules a timer at that boundary;
  on fire it snapshots equity and persists the snapshot to the journal.

### Leverage
- Platform max: 5x BTC/ETH, 2x other crypto, 4x equities/commodities.
- Internal caps sit BELOW the platform max (3x BTC/ETH, 2x other crypto by
  default in `config/runtime.yaml`).

### Venue
- Exchange is hardcoded to `hyperliquid`, productType `perp`. Perpetuals only.
  Funding rates apply.
- 24/7 market.

### Rate limit
- 1,200 req/min flat. Token bucket reserves 400 req/min for emergency flattens;
  normal traffic is capped at 800/min.

## Architecture

### Pure C++. No Python in the runtime.
Single binary `propr_agent` plus per-strategy `.so` plugins. Backtest is a
separate binary `propr_backtest` linking the same risk and strategy libraries.

```
+-----------------+         +-----------------+
| Hyperliquid WS  | ticks   |   Propr WS      | account/order/pos
+-----------------+         +-----------------+
        \                          /
         \                        /
          v                      v
        +----------------------------+
        |          EventBus          |   (lock-free MPSC)
        +----------------------------+
                       |
                       v
            +------------------------+
            |     Strategy plugin    |   emits IntentV1
            +------------------------+
                       |
                       v
            +------------------------+
            |     RiskEngine         |   consults StateMachine, KillSwitch,
            |                        |   RateLimiter, LeverageCap, Sizing
            |    -> RiskDecisionV1   |   emits signed OrderCommandV1
            +------------------------+
                       |
                       v
            +------------------------+
            |     OrderManager       |   verifies HMAC, expiry, state machine
            |                        |   journals BEFORE submit
            +------------------------+
                       |
                       v
            +------------------------+
            |    OrderExecutor       |   interface; one impl per backend
            +----------+---+---------+
                       |   |
              +--------+   +---------+
              v                      v
       +------------+         +-------------+
       | PropreHttp |         | SimExecutor |
       | Executor   |         | + Simulator |
       +------------+         +-------------+
```

### Versioned IPC schemas
Every cross-module message lives in `schemas/v1.h` with an explicit `v` field
and JSON round-trip. If a struct is not in `schemas/v1.h`, it does not cross a
module boundary.

Types: `TickV1`, `AccountSnapshotV1`, `IntentV1`, `OrderCommandV1`,
`RiskDecisionV1`, `ExecutionReportV1`, `FillV1`, `PositionUpdateV1`,
`KillSwitchTripV1`, `ReconcileDivergenceV1`, `DailyResetV1`, `WsStatusV1`.

Enums serialize as strings. `std::optional` fields are absent from JSON when
unset.

To make a breaking schema change: bump `kVersion`, add `schemas/v2.h`, keep
`v1.h` for replay. Never reorder fields in `canonical_for_hmac` without bumping.

### Explicit reconciliation state machine
`app::StateMachine` exposes one of six states. Every decision that touches the
network or an order consults it; nothing else is allowed to gate trading.

```
STARTING -> RECONCILING -> LIVE -+-> BLIND ----+-> FLATTENING -> HALTED
                                 |             |
                                 +<- LIVE <----+
                                 +-> FLATTENING -> HALTED
any -> HALTED
```

- `LIVE`: only state that allows new entries.
- `BLIND`: WS disconnect or reconcile divergence. Flatten allowed, new entries
  rejected with `RiskReasonV1::StateNotLive`.
- `FLATTENING`: kill switch tripped, flatten in flight.
- `HALTED`: terminal. Operator restart required.

### Signed risk decisions
`RiskEngine::evaluate(IntentV1) -> RiskDecisionV1` produces a decision whose
`command` field (when present) is an `OrderCommandV1` signed with HMAC-SHA256
over a canonical encoding (`schemas/sign.cpp::canonical_for_hmac`).

The OrderManager refuses any command where:
- `hmac_hex` does not match the session secret;
- `now_ns > expires_at_ns`;
- the StateMachine does not currently allow trading.

This separates "risk approved" from "order placed". Bugs that create
`OrderCommandV1` values outside the RiskEngine are rejected at the executor.

### Event-sourced journal
SQLite WAL at `logs/journal.db`. Tables:
- `journal_events`: append-only stream of every consequential event.
- `intents`: idempotency record per OrderCommand (`command_uuid -> {ULIDs}`).
- `account_snapshots`: daily reset + on-trip snapshots.
- `strategy_state`: opaque per-strategy blob for resume.

On boot:
1. Open journal. Refuse to start on schema version mismatch.
2. `last_snapshot("daily_reset")` seeds the daily snapshot.
3. `replay_since(last_at_ns, handler)` rebuilds the in-memory mirror.
4. REST reconcile catches up any gap.
5. `OrderManager::retry_unresolved()` reclaims dangling intents (Propr
   deduplicates by `intentId` server-side).

### Preflight green-check list
`app::Preflight` runs an ordered gate list before the StateMachine flips
`RECONCILING -> LIVE`. First failing gate aborts and the app HALTs.

Gates (in order):
- `health_ok` - `GET /health` returns OK
- `services_ok` - `GET /health/services` returns `core: OK`
- `challenge_loaded` - `ChallengeRules.max_overall_dd_abs > 0` and
  `max_daily_loss_abs > 0`
- `account_discovered` - non-empty `AccountId`
- `leverage_limits_loaded` - per-asset cap returned
- `daily_snapshot_present` - `RiskEngine::daily_snapshot() > 0`
- `rest_ws_reconciled` - REST snapshot matches running mirror
- `market_feed_live` - Hyperliquid WS event within 30s
- `journal_writable` - dry-run write succeeds
- `kill_switch_clean` - not armed at startup

## Simulator-first development
Before any code touches Propr, the risk core is proven against
`sim::ExchangeSimulator`. The simulator is deterministic given its seed and
exposes knobs for every misbehavior class:

- `rejection_probability` - synthetic 5xx
- `rate_limit_probability` - synthetic 429
- `partial_fill_probability` - entry fills at 50% requested qty
- `fill_skip_probability` - silently drop the fill
- `drop_snapshot_probability` - lose the next AccountUpdated
- `duplicate_snapshot_probability` - publish twice
- `stale_snapshot_ticks` - delay snapshots by N ticks
- `fill_latency_ticks` - delay fills by N ticks
- `taker_fee_bps`, `slippage_bps` - economics

`tests/integration/test_simulator_drill.cpp` builds the full risk pipeline and
proves the kill switch trips and FLATTENING completes under adversarial input.

`make simulator-drill` is a release gate. `make kill-switch-drill` is the
narrow path test.

## Risk philosophy (the actual edge)

- Headroom-based sizing. Compute `equity`, `daily_headroom` (vs UTC midnight
  snapshot), `overall_headroom` (vs HWM minus max_overall_dd). Risk per trade
  `<= min(2% equity, 25% daily_headroom, 10% overall_headroom)`.
- Floating-loss kill switch trips at 70% of overall DD consumed.
- Daily-loss kill switch trips at 70% of daily headroom consumed.
- WS-disconnect kill switch trips after 5s of silence; reconcile divergence
  >3 in 5 minutes also trips.
- Token bucket: 800/min normal, 400/min reserved for flatten path.
- Marketable-limit entries with `slippage_buffer_bps` baked into the price.
  The Propr API has no `max_slippage` parameter, so the limit price IS the
  guard. Track realised slippage from `FillV1.slippage_micro`.
- 0.075% taker AND maker. Round-trip ~15bps before slippage. Strategies must
  clear ~25bps net.

## Repo layout

```
propr-agent/
  CLAUDE.md
  README.md
  CMakeLists.txt
  vcpkg.json
  Makefile
  config/
    runtime.yaml
    .env.example
  include/propr/
    schemas/v1.h           (canonical wire format + JSON)
    schemas/sign.h         (HMAC sign/verify)
    core/                  (types, result, ulid, clock, events, event_bus, sha256)
    account/               (account, position, challenge_rules)
    risk/                  (rate_limiter, sizing_policy, leverage_cap,
                            kill_switch, risk_engine)
    exec/                  (order_executor, order_manager,
                            propre_http_executor)
    net/                   (http_client, propr_ws, hyperliquid_ws)
    strategy/              (strategy, market_snapshot, plugin_loader)
    sim/                   (exchange_simulator, sim_executor)
    persist/               (journal, schema)
    app/                   (state_machine, preflight, trading_app)
    config/, log/
  src/                     (impl, mirrors include/propr/)
  app/                     (live runtime binary)
  backtest/                (offline binary)
  strategies/range_mr/     (one boring strategy behind the risk engine)
  tests/
    unit/                  (gtest)
    integration/           (kill-switch drill + simulator drill)
    fakes/                 (fake_clock.h, fake_http_client.h)
  data/, logs/             (gitignored)
```

## Build and run

```
export VCPKG_ROOT=/path/to/vcpkg
make build
make test                   # full unit + integration
make kill-switch-drill      # narrow release gate
make simulator-drill        # broad release gate (alias for test_simulator_drill)
PROPR_API_KEY=pk_live_... make smoke-net    # GET /health round-trip
make run                    # live, requires PROPR_API_KEY
```

## Testing

Unit:
- `test_ulid` - monotonicity, thread-safety, Crockford alphabet
- `test_clock` - UTC midnight rounding
- `test_rate_limiter` - token refill, reserved isolation
- `test_sizing_policy` - cap math
- `test_account_equity` - the formula, HWM monotonicity, margin headroom
- `test_kill_switch` - all four trip paths, arm-once idempotence
- `test_journal_replay` - round-trip, intent idempotency, snapshot recall
- `test_state_machine` - every transition, listener fires
- `test_preflight` - first-failure stop, ordering
- `test_schemas_roundtrip` - JSON stability across V1 types
- `test_signed_command` - tamper detection on every signed field
- `test_risk_engine` - approve, reject, signed command, state-not-live, kill armed
- `test_invariants` - seeded-random property tests

Integration:
- `test_kill_switch_drill` - adverse equity tick triggers FLATTENING + HALTED
  within 1s
- `test_simulator_drill` - end-to-end risk core under adversarial simulator
  (drops, dupes, 429s, partial fills); proves entry rejected in BLIND

Strategies are deliberately not unit-tested. The drills exercise them through
the simulator.

## Wiring debts to settle before the next live run

The risk core is proven against the simulator. The live wiring still needs
finishing:

1. `src/app/trading_app.cpp` was written against the older RiskEngine API.
   Update it to:
   - Construct `StateMachine` and `Preflight` in `bootstrap()`.
   - Pass `sm`, `ulid`, `hmac_secret`, and `RiskEngine::Config` to the
     `RiskEngine` constructor.
   - Use `PropreHttpExecutor` (live) or `sim::SimExecutor` (`--sim` flag).
   - Mint a per-session HMAC secret if `hmac_secret` config is blank.
   - Run `Preflight::run()`. Transition `RECONCILING -> LIVE` only on green.
2. `app/main.cpp` needs an `--sim` flag (drive simulator without REST/WS).
3. `backtest/main.cpp` needs to be updated to construct the new RiskEngine and
   use `IntentV1` / `RiskDecisionV1`.
4. `src/net/propr_ws.cpp` only translates `account.updated` today. Translate
   `order.*`, `position.*`, `trade.created` once we have one real payload of
   each to confirm field names against.
5. The `cancel-all` endpoint path (`/orders/cancel-all`) is a guess. Confirm
   against `propr.xyz/docs/bot` before live trading.
6. Top-level `vcpkg.json` has a placeholder `builtin-baseline`. Replace with a
   real SHA from `microsoft/vcpkg`.

## Gotchas to never forget

- Equity is a client-side computation. There is no equity field in the API.
- Floating P&L counts toward DD. A 4% paper loss is already eating headroom.
- 00:00 UTC is the only clock that matters for daily reset.
- Propr WS has no replay and no sequence numbers. Reconnect = reconcile via REST.
- Propr WS has no market data. Use Hyperliquid public WS for ticks.
- `intentId` is the dedup key. Mint once, reuse on retry. Propr dedupes server-side.
- Batched orders need an `orderGroupId`. Only ONE entry per group.
- 5x BTC/ETH leverage is the PLATFORM max, not your target.
- 0.075% taker AND 0.075% maker. Maker is NOT free.
- No `max_slippage` parameter on order create. Use marketable-limit + IOC.
- `market_sell` and `limit_sell` in the Python SDK default `reduceOnly=True`.
  We do not use the SDK, but the lesson stands: reduce-only entries do not open
  shorts.

## Resources

- Propr API docs portal: https://propr.xyz/docs/bot
- Canonical docs source: https://github.com/XBorgLabs/propr-docs
- OpenAPI spec: https://propr.xyz/openapi.json
- Sign-up / dashboard: https://app.propr.xyz
- Hyperliquid public WS (market data):
  https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/websocket
