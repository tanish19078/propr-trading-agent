# Propr Agent Engineering Review

Date: 2026-05-29

This document captures the current repo state, corrections needed to make the
project build and run safely, and suggested changes before any paper or live
Propr account is connected.

## Executive Summary

The project has the right safety-first architecture: C++20 runtime, account
mirror, explicit state machine, risk engine, signed order commands, rate limiter,
kill switch, SQLite journal, simulator, strategy plugin ABI, and tests around the
highest-risk surfaces.

The repo is still mid-refactor. The canonical schema layer has moved to
`schemas::v1`, but the runtime app and backtest still contain older API calls.
The next milestone should be a green local build and green test suite, not new
strategy work.

## Current Strengths

- `account::Account` implements the critical equity formula:
  `equity = balance + totalUnrealizedPnl + isolatedPositionMargin`.
- `risk::RiskEngine` gates entries by app state, kill switch, rate limit,
  headroom-based sizing, leverage cap, and margin.
- `schemas::v1` is a good canonical boundary for intents, decisions, commands,
  fills, snapshots, and execution reports.
- Signed `OrderCommandV1` prevents executor-side tampering and gives a clear
  separation between "risk approved" and "order placed".
- `OrderManager` verifies HMAC, rejects expired commands, journals issued
  commands before execution, and preserves unresolved commands for recovery.
- `StateMachine` makes lifecycle states explicit: starting, reconciling, live,
  blind, flattening, halted.
- `ExchangeSimulator` is the right direction for local hostile testing before
  any Propr API integration.
- `range_mr` is a safer starter strategy than the old DCA grid: one entry, one
  stop, one take profit, no averaging down.

## Build Environment Corrections

These are needed before compile/test feedback is reliable.

1. Install or expose CMake on PATH.
2. Install or expose vcpkg on PATH and set `VCPKG_ROOT`.
3. Install or expose `make`, or document the PowerShell-native equivalent:
   `cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=...` and
   `cmake --build build`.
4. Initialize the folder as a Git repository or clone it as one. Current
   `git status` fails because `.git` is missing.

Suggested check:

```powershell
cmake --version
vcpkg version
make --version
git status --short
```

## Must Fix To Compile

### 1. Remove stale `propr/strategy/intent.h` includes

The file does not exist, but two files still include it:

- `include/propr/risk/sizing_policy.h`
- `backtest/fake_account.h`

Correction:

- Remove the include from `sizing_policy.h`; that header does not use strategy
  intent types.
- Port `backtest/fake_account.{h,cpp}` from old `strategy::Intent` to
  `schemas::v1::IntentV1` or to `schemas::v1::OrderCommandV1`.

Preferred direction:

- Backtest should exercise the same live path:
  `IntentV1 -> RiskEngine -> OrderCommandV1 -> OrderManager -> SimExecutor`.
- If `FakeAccount` remains, it should consume `OrderCommandV1`, not strategy
  intents.

### 2. Add simulator sources to the core build

`src/sim/exchange_simulator.cpp` is not included in `PROPR_CORE_SRC`.

Correction in `CMakeLists.txt`:

```cmake
file(GLOB_RECURSE PROPR_CORE_SRC CONFIGURE_DEPENDS
  src/core/*.cpp
  src/account/*.cpp
  src/risk/*.cpp
  src/exec/*.cpp
  src/strategy/*.cpp
  src/persist/*.cpp
  src/config/*.cpp
  src/log/*.cpp
  src/net/*.cpp
  src/app/*.cpp
  src/sim/*.cpp
)
```

Without this, simulator integration tests can compile but fail to link.

### 3. Update `TradingApp` to the current constructor signatures

`TradingApp` still calls old constructors.

Current `RiskEngine` requires:

```cpp
RiskEngine(account, rules, state_machine, kill, rate, leverage, sizing,
           ulid, clock, hmac_secret, cfg)
```

Current `OrderManager` requires:

```cpp
OrderManager(order_executor, journal, state_machine, clock, hmac_secret)
```

Corrections:

- Add `app::StateMachine state_machine_;` to `TradingApp`.
- Add `exec::PropreHttpExecutor executor_;` to `TradingApp`.
- Add a runtime HMAC/session secret. It can be generated on startup with ULID or
  read from config/env, but it must be stable for the lifetime of the process.
- Construct `RiskEngine` with `state_machine_`, `ulid_`, `clock_`, and the HMAC
  secret.
- Construct `OrderManager` with `executor_`, not `HttpClient`.

### 4. Update strategy dispatch in `TradingApp`

Old code still checks `risk::RiskEngine::Outcome::Reject` and calls
`order_manager_.send(...)`.

Correction:

```cpp
auto decision = risk_engine_.evaluate(intent);
if (decision.outcome == schemas::v1::RiskOutcomeV1::Reject) {
  // journal/log rejection
  return;
}
if (!decision.command.has_value()) {
  // non-entry approved intent or invalid state; handle explicitly
  return;
}
auto report = order_manager_.execute(*decision.command);
```

Also update all `flatten_all()` calls to pass the account:

```cpp
order_manager_.flatten_all(account_);
```

### 5. Convert core events before strategy callbacks

The strategy ABI expects:

- `schemas::v1::FillV1`
- `schemas::v1::PositionUpdateV1`

`TradingApp::on_event_` currently passes:

- `core::Fill`
- `core::PositionUpdate`

Correction:

- Add conversion helpers:
  `schemas::v1::FillV1 to_schema(const core::Fill&)`
  and
  `schemas::v1::PositionUpdateV1 to_schema(const core::PositionUpdate&)`.
- Call strategies with the schema types only.

### 6. Assign non-empty `intent_uuid` before risk evaluation

`range_mr` emits `intent_uuid = ""`, but `RiskEngine` copies
`intent.intent_uuid` directly into the decision and command.

Correction:

- In `TradingApp`, after a strategy emits an intent and before calling
  `risk_engine_.evaluate`, set:

```cpp
if (intent.intent_uuid.empty()) {
  intent.intent_uuid = ulid_.next();
}
```

Alternative:

- Pass an intent factory/context into strategies, but that is a larger ABI
  change. For now, assign in the app.

## Must Fix Before Paper Or Live Trading

### 1. Health gates are incomplete

Bootstrap checks `/health` but not `/health/services`.

Correction:

- Require both:
  - `GET /health` returns `{"status":"OK"}`
  - `GET /health/services` returns `{"core":"OK"}`
- Fail closed if either response is missing, malformed, or not OK.

### 2. Preflight exists but is not wired into bootstrap

`Preflight` is implemented and tested, but `TradingApp::bootstrap` does not use
it.

Correction:

- Transition `Starting -> Reconciling` at bootstrap start.
- Build preflight gates for:
  - health OK
  - services OK
  - active account discovered
  - challenge rules loaded and positive
  - account snapshot loaded
  - daily snapshot present
  - journal writable
  - strategy plugins loaded
  - Hyperliquid WS connected or simulator mode active
  - Propr WS connected or explicit offline/sim mode
- Only transition `Reconciling -> Live` after all gates pass.
- On failure, transition to `Halted`.

### 3. Reconciler only reconciles account balance

Current reconciler fetches only `/accounts/{id}`.

Correction:

- Reconcile:
  - `/accounts/{id}`
  - `/positions`
  - `/orders?status=open`
- Detect divergence in balance/equity, positions, and open orders.
- On reconnect from Propr WS, enter `Blind`, cancel/flatten if needed, and only
  return to `Live` after full REST reconciliation passes.

### 4. Propr WS event translation is incomplete

`ProprWebSocket` handles `account.updated` only. Order, position, and trade
events are not translated.

Correction:

- Translate:
  - `order.created`
  - `order.updated`
  - `order.cancelled`
  - `order.filled` / partial fills if emitted
  - `position.opened`
  - `position.updated`
  - `position.closed`
  - `trade.created`
- Publish typed core events or schema events.
- Journal raw payloads for unknown event types.

### 5. Challenge parsing uses guessed field names

`TradingApp` currently reads likely field names such as `profitTarget`,
`maxDrawdown`, and `maxDailyLoss`.

Correction:

- Add typed challenge parsing with explicit accepted field aliases.
- Log the raw challenge object once at startup.
- Fail closed if required thresholds cannot be parsed.
- Add a unit test with the real live API response once available.

### 6. Decimal parsing uses floating point

Live money and price parsing uses `std::stod` and `get<double>()`.

Correction:

- Add decimal string parsers:
  - `parse_money_micro(std::string_view)`
  - `parse_price_micro(std::string_view)`
- Reject or fail closed on malformed decimal input.
- Avoid binary floating point in account/risk/order paths.

Acceptable exception:

- Strategy indicators and simulator probability knobs may use `double`; account
  balances, PnL, margin, order price, quantity, and notional should not.

### 7. `range_mr` uses double for price math

This is acceptable for research/indicator logic, but the final intent price
conversion should be explicit and conservative.

Correction:

- Compute bands in `double` if needed.
- Clamp and convert with helper functions.
- Ensure stop is always on the correct side:
  - long stop below entry
  - short stop above entry
- Set `risk_at_stop_micro` in the intent for logging and review, even if the
  risk engine recomputes sizing.

## Suggested Architecture Changes

### 1. Make state transitions visible and journaled

Every state transition should write a structured journal event:

```json
{"from":"RECONCILING","to":"LIVE","reason":"preflight_ok"}
```

This makes postmortems and restart behavior much easier.

### 2. Add command type to `OrderCommandV1`

`OrderManager` currently cannot distinguish entry commands from flatten-class
commands just from `OrderCommandV1`.

Suggested schema addition:

```cpp
enum class CommandKindV1 { EntryBracket, CancelAll, Flatten };
CommandKindV1 kind;
```

Then `OrderManager` can allow flatten commands in `Blind` / `Flattening` without
using `allows_new_entries()`.

### 3. Make close/cancel commands signed too

`flatten_all` calls executor methods directly. That is fine for a first drill,
but the release version should make emergency actions auditable and signed.

Suggested change:

- Risk/kill path emits `CancelAllCommandV1` and `ClosePositionCommandV1`.
- `OrderManager` verifies and journals them exactly like entries.

### 4. Move event conversions into one module

Suggested file:

```text
include/propr/schemas/convert.h
src/schemas/convert.cpp
```

Conversions:

- `core::Fill -> FillV1`
- `core::PositionUpdate -> PositionUpdateV1`
- `core::AccountUpdated -> AccountSnapshotV1`
- `core::KillSwitchTrip -> KillSwitchTripV1`

### 5. Add simulator mode to the app binary

Add:

```text
propr_agent --sim --config config/runtime.yaml
```

This mode should wire:

- `ExchangeSimulator`
- `SimExecutor`
- no Propr HTTP
- optional Hyperliquid WS or deterministic CSV feed

This gives a safe demo mode and CI drill target.

### 6. Split build targets by risk level

Suggested Makefile targets:

```make
make unit
make integration
make simulator-drill
make smoke-net
make live
```

`live` should require:

- `PROPR_API_KEY`
- `CONFIRM_LIVE=I_UNDERSTAND_THIS_CAN_TRADE`
- green preflight

## Test Additions

Add or keep these tests as release gates:

1. `TradingApp` bootstrap fails if `/health/services` is not OK.
2. `TradingApp` does not enter `Live` if challenge rules are missing.
3. `TradingApp` assigns missing `intent_uuid`.
4. `OrderManager` rejects expired commands.
5. `OrderManager` rejects bad HMAC.
6. `OrderManager` allows signed flatten commands in `Blind`.
7. Reconciler trips kill switch after 3 position/order divergences.
8. Propr WS unknown events are journaled, not ignored silently.
9. Decimal parser handles:
   - `"123"`
   - `"123.45"`
   - `"0.000001"`
   - negative PnL
   - too many decimals
   - malformed strings
10. Full simulator drill:
    - entry approved
    - partial fill handled
    - account mirror updated
    - adverse tick trips kill switch
    - cancel/close emitted
    - state ends `Halted`

## Suggested Work Order

### Sprint 1: Green Build

1. Remove stale `intent.h` includes.
2. Add `src/sim/*.cpp` to CMake.
3. Update `TradingApp` constructor wiring.
4. Update strategy dispatch to `RiskOutcomeV1` and `OrderManager::execute`.
5. Convert core fill/position events to schema types.
6. Fix backtest API drift or temporarily exclude `backtest` from default build.
7. Run all tests.

Definition of done:

- `make build` passes.
- `make test` passes.
- No stale `strategy::Intent` references remain.

### Sprint 2: Safe Simulator Mode

1. Add `--sim`.
2. Use `ExchangeSimulator` and `SimExecutor`.
3. Feed deterministic ticks.
4. Journal all decisions/reports.
5. Run simulator drill in CI.

Definition of done:

- `propr_agent --sim` runs without API keys.
- Simulator drill proves flatten-on-loss end to end.

### Sprint 3: Live API Readiness

1. Complete `/health/services`.
2. Complete typed challenge parsing.
3. Complete account/positions/open-orders reconciliation.
4. Complete Propr WS event translation.
5. Replace `std::stod` in risk/account/order paths.

Definition of done:

- App refuses to trade unless all live gates are green.
- WS reconnect forces `Blind` and full REST reconciliation.
- Unknown or malformed live API fields fail closed.

### Sprint 4: Strategy Research

1. Backtest `range_mr` on at least 12 months of BTC/ETH minute data.
2. Include fees, slippage, funding, and Propr daily/overall DD rules.
3. Produce `docs/strategy.md`.
4. Keep DCA/grid strategies out of live until they survive regime tests.

Definition of done:

- Strategy report includes Sharpe, max drawdown, longest losing streak,
  days-to-recover, fee drag, funding drag, and challenge survival probability.

## Live-Account Rule

Do not connect a live Propr key until all of these are true:

- Build is green.
- Unit and integration tests are green.
- Simulator drill is green.
- Reconciler handles account, positions, and open orders.
- Propr WS translates fills and positions.
- Challenge rules are parsed from live API response and logged.
- App can run in simulator mode for at least 24 hours without state drift.
- `range_mr` or any other strategy has a committed backtest report.

The project is moving in the right direction. The next best engineering move is
not adding more strategies. It is making the safety pipeline compile, run, and
prove itself under simulation.
