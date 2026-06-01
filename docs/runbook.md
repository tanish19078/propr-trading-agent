# Demo runbook

The path from "I have a Propr API key" to "I'm confident enough to buy a paid
5K Starter ($60)". Every step has a pass criterion; do not move forward
without it.

## Important reality check

There is no separate `pk_beta_...` key tier for normal users. Both demo
(paper) trading and live (funded) trading share the same API host
(`api.propr.xyz`) and the same `pk_live_...` key format. **The distinction is
the challenge attempt's `account.type` field**:

- `"paper"` -> the Free Trial challenge. Real API, simulated PnL, no money.
- `"book"` / `"a_book"` -> a paid Starter/Silver/etc. challenge. Real PnL.

So our `--profile=beta` flag is currently a no-op for normal users. Use
`--profile=live` everywhere; the agent reads `account.type` and refuses to
treat a paper account as funded (and vice versa).

## 0. Prereqs

- Docker installed and running (recommended).
- A Propr account at https://app.propr.xyz with an API key from Settings
  (format `pk_live_...`).
- An active "Free Trial" challenge (free, up to 3 attempts per user) bought
  from the challenges page. The agent auto-discovers it.

## 1. `.env`

```
PROPR_API_KEY=pk_live_...your_key...
PROPR_PROFILE=live
PROPR_HMAC_SECRET=
```

`.env` is gitignored. Never paste the key into chat, GitHub issues, or
screenshots.

## 2. Build the image

```
make docker-build
make docker-test
```

Pass criteria:
- All unit tests green
- `test_simulator_drill` green (the risk core under hostile sim conditions)
- `test_kill_switch_drill` green
- `test_decimal` green (parsing real Propr decimal strings)

If anything fails, do NOT proceed to the network. Fix it.

## 3. Smoke-test against the live API

```
make docker-smoke
```

Three round-trips against `api.propr.xyz`:
1. `GET /health` -> `{"status": "OK"}`
2. `GET /health/services` -> `{"gateway":"OK","core":"OK"}`
3. `GET /users/me` -> your user object

If `users/me` returns 401 the key is wrong or revoked.

## 4. Confirm you have a Free Trial active

Visit https://app.propr.xyz, page through to the challenges section, and
purchase "Free Trial" (price = $0). It becomes the active attempt
immediately.

You can also verify from your own machine:
```
curl -sS -H "X-API-Key: $PROPR_API_KEY" \
  "https://api.propr.xyz/v1/challenge-attempts?status=active&limit=1" | jq .
```
The response should contain `"data":[{...,"status":"active","account":{"type":"paper",...}}]`.

## 5. Boot the agent

```
make docker-shell
# inside the container:
make run PROFILE=live
```

Pass criterion: bootstrap walks every preflight gate, logs the raw challenge
shape, and the state machine transitions `STARTING -> RECONCILING -> LIVE`.
You should see:

- `{"bootstrap":"start","profile":"live"}`
- `{"challenge_raw":{...phases:[{order:1,profitTargetPercent:"10",...}]}}`
- `{"propr_ws_connected":true}`
- `{"bootstrap":"live"}`

If preflight fails, the log line tells you which gate. Common ones:

| Gate | Cause | Fix |
|---|---|---|
| `health_ok` | API down or unreachable | retry; check network |
| `challenge_loaded` | No active attempt | buy Free Trial at app.propr.xyz |
| `daily_snapshot_present` | Initial equity 0 | inspect challenge object in logs |
| `journal_writable` | logs/ not writable | check filesystem perms |

Also: if `drawdownType == "trailing"` we deliberately HALT and refuse to
trade. The v1 risk core only models static drawdown. Buy a static-DD
challenge ("Free Trial", "Starter") rather than a trailing-DD one ("Silver",
"Gold") for now.

## 6. Watch range_mr trade for 1 hour

```
make docker-shell  # in another terminal
tail -f logs/agent.jsonl | jq
```

What to verify:

- `range_mr` emits intents; `risk_decision_reject` lines explain rejections
  (state, kill-switch, sizing).
- Every fill carries a `slippage` field; average under
  `slippage_buffer_bps` (default 20 bps).
- Daily reset fires once at 00:00 UTC.

## 7. Adverse drill against the demo

Forcing the kill switch on a paper account requires the position math to
move adversely:
1. Manually open an oversized adverse position via the Propr dashboard so
   the account equity drops fast.
2. Watch for `{"kill_switch_trip":..., "reason":"floating"}`.
3. Verify the state machine moved to `FLATTENING` then `HALTED`.
4. Verify the position is flat via Propr dashboard.

Pass: kill switch trips, flatten completes, state reaches HALTED, no open
positions remain.

## 8. Soak run

Run >= 48h on the demo. Pass criteria:

- No bootstrap failures
- Zero reconcile divergences after the first 5 minutes
- All UTC midnight resets fire cleanly
- `intentId` values reused on retry (search journal for duplicates; only one
  per logical intent)
- Net P&L on the soak is at worst flat (+/- 1% of starting equity)

## 9. Buy a paid Starter

Only after Step 8 passes:

1. Buy "Starter" ($60, 5K, 1-step, 10% profit / 6% DD / 3% daily, static) at
   https://app.propr.xyz.
2. The same `pk_live_...` key already works; no need to rotate.
3. The agent auto-discovers the new active attempt. Same code, same risk
   caps, real PnL.

## Ready-to-buy checklist

All true:

- [ ] `make docker-test` green
- [ ] `make docker-smoke` returns 200 on all three calls
- [ ] Agent ran >= 48h on Free Trial demo, no manual restarts
- [ ] At least one demo kill-switch trip observed end-to-end and recovered
- [ ] Slippage averages under `slippage_buffer_bps`
- [ ] No duplicate `intentId` in journal
- [ ] Net P&L on demo within +/- 1%
