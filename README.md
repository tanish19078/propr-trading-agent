# propr-agent

Pure C++ trading agent targeting a Propr 1-Step challenge.

Read [`CLAUDE.md`](./CLAUDE.md) before changing anything — it documents the platform constraints, the risk model, and the architectural invariants.

## Build

Requires CMake ≥ 3.22, a C++20 compiler, and [vcpkg](https://github.com/microsoft/vcpkg).

```bash
export VCPKG_ROOT=/path/to/vcpkg
make build
make test
```

## Layout

| Path | What lives there |
|---|---|
| `include/propr/` | Public headers, organised by module |
| `src/` | Implementation, mirrors `include/propr/` |
| `app/` | Live runtime binary (`propr_agent`) |
| `backtest/` | Offline backtest binary (`propr_backtest`), reuses the same modules |
| `strategies/<name>/` | One shared library per strategy, loaded at runtime |
| `tests/{unit,integration}/` | GoogleTest |
| `tests/fakes/` | In-process fakes for HTTP, WS, Clock |
| `config/` | YAML config, `.env.example` |
| `scripts/` | One-off C++ utilities (`fetch_history.cpp`, etc.) |

## Modules

The core invariant: **strategies emit `Intent`s, never orders**. Every intent flows `Strategy → RiskEngine → OrderManager → HttpClient`. The RiskEngine is the gatekeeper that makes `max_drawdown_exceeded` and `max_daily_loss_exceeded` impossible by construction.

See [`docs/architecture.md`](./CLAUDE.md#architecture) (in `CLAUDE.md`) for the dataflow diagram and threading model.

## Running a challenge

Follow [`docs/runbook.md`](./docs/runbook.md). The TL;DR:

1. Generate a demo key at [app.beta.propr.xyz](https://app.beta.propr.xyz).
2. `cp config/.env.example .env`, paste the key, set `PROPR_PROFILE=beta`.
3. `make docker-build` (one-time).
4. `make docker-test` -- green is mandatory before any network call.
5. `make docker-smoke PROFILE=beta` to confirm the key works.
6. `make run PROFILE=beta` to start the agent against the demo.
7. Soak for 48h on demo. Only then buy a paid 5K 1-Step at
   [app.propr.xyz](https://app.propr.xyz) and `make run PROFILE=live`.

## Don't

- Don't add Python to the runtime. Backtests live in `backtest/`, same C++.
- Don't bypass the RiskEngine. Strategies emit intents, full stop.
- Don't use local time anywhere on the risk path. UTC everywhere.
- Don't use floats for money. `Money` is `int64_t` in micro-USDC (`1 USDC = 1_000_000`).
