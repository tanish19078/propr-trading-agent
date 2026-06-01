# Demo Run Findings — 2026-05-30

**Run duration:** ~76 minutes (12:28:05 → 13:44:49 UTC equivalent)  
**Account:** Free Trial paper (api.propr.xyz)  
**Journal:** `logs/journal.db` (150 events)  
**Outcome:** Kill switch tripped at T+4.8s, process exited, all subsequent intents rejected

---

## Executive Summary

The agent reached LIVE state successfully but the kill switch tripped **4.8 seconds later** due to a false WebSocket disconnect alarm. The root cause: `ProprWebSocket::last_event_ns_` initializes to 0 and is only updated when **data messages** arrive, not on the `Open` event. On a quiet paper account with no positions or orders, the Propr WS sends no traffic after the initial handshake, so `check_ws_disconnect()` saw a gap exceeding the 5000ms threshold and armed the kill switch.

Once armed, all 147 subsequent strategy intents were rejected with `reason: kill_switch_armed`. The strategy logic itself is correct; the rejects are not due to sizing or daily_snapshot issues.

---

## Timeline (relative to bootstrap "live" at T+0s)

| Offset | Event | Detail |
|--------|-------|--------|
| T-0.8s | Propr WS connected | `propr_ws_connected:true` in stderr |
| T+0s | State → LIVE | Preflight green, `bootstrap:"live"` |
| T+4.8s | **Kill switch trip** | `reason:"ws"`, detail: `propr silent for 5012ms` |
| T+4.8s | Flatten attempt (cancel-all) | 404: `Cannot POST /v1/accounts/.../orders/cancel-all` |
| T+77min | Actual WS disconnect | Propr WS dropped, reconnected in 1.7s |
| T+77–88min | 147 intents rejected | All `kill_switch_armed`, detail: `ws_disconnect:propr silent for 5012ms` |

---

## Bug 1: False WS-disconnect kill switch trip (BLOCKER)

### What happened

`kill_switch.cpp::check_ws_disconnect()` compares `now - last_event_ns` against `ws_blind_mode_after_ms` (5000ms). At T+4.8s it saw a gap of 5012ms and armed the kill switch.

### Root cause

`ProprWebSocket::last_event_ns_` (propr_ws.h:45) initializes to **0** and is updated **only** in `handle_text_()` (propr_ws.cpp:93) — i.e., when an actual data message arrives. It is **not** updated on the WebSocket `Open` event.

On a Free Trial paper account with no open positions or orders, the Propr WS sends:
- The initial handshake (triggers `Open` callback)
- **No subsequent data messages** until account state changes

So the sequence was:
1. WS connects at T-0.8s → `Open` fires, `last_event_ns_` still 0
2. Bootstrap completes at T+0s → LIVE
3. Main loop ticks at T+4.8s → `check_ws_disconnect(last_event_ns=0, "propr")` sees `gap = now - 0 = ~5012ms` → trip

The 13:44 disconnect (T+77min) was a real 1.7-second blip, well under the 5000ms threshold, and harmless. The handoff inverted cause and effect.

### Fix

**Option A (recommended):** Update `last_event_ns_` on `Open` as well as on data messages.

```cpp
// propr_ws.cpp:68
case WebSocketMessageType::Open:
  last_event_ns_.store(clock_.now_ns(), std::memory_order_release);  // ADD THIS
  PROPR_LOG_INFO(R"({"propr_ws_connected":true})");
  bus_.publish(core::WsReconnect{.ws_name = "propr", .at_ns = clock_.now_ns()});
  break;
```

**Option B:** Disable the WS-disconnect kill switch entirely for paper accounts (they have no real capital at risk). Check `account.type == "paper"` in `trading_app.cpp::tick_()` and skip the `check_ws_disconnect()` call.

**Option C:** Raise `ws_blind_mode_after_ms` to 60000 (1 minute) to tolerate longer silence on quiet accounts. This is a band-aid; Option A is the real fix.

---

## Bug 2: cancel-all endpoint 404 (MEDIUM)

### What happened

The flatten attempt at T+4.8s returned:
```json
{"detail": "{\"message\":\"Cannot POST /v1/accounts/urn:prp-account:jK4TCsv9ZiNh/orders/cancel-all\"}"}
```

### Root cause

The cancel-all endpoint path is currently a guess (CLAUDE.md:325). The actual endpoint may be:
- `POST /orders/cancel-all` (no `/accounts/{id}` prefix), or
- `DELETE /orders` with query params, or
- Not implemented yet on the Propr API

### Fix

1. Check https://propr.xyz/docs/bot and https://propr.xyz/openapi.json for the correct cancel-all path.
2. If no cancel-all exists, implement flatten as: `GET /orders?status=open` → iterate → `DELETE /orders/{orderId}` per order.
3. Update `propre_http_executor.cpp` (or the OrderManager flatten path) accordingly.

**Impact:** On a paper account with no open orders, this 404 is harmless. On a funded account with open positions when the kill switch trips, this is a **critical failure** — the flatten won't execute and the account remains exposed.

---

## Bug 3: daily_snapshot never written (LOW — not the cause of rejects)

### What happened

`account_snapshots` table is empty. No daily reset snapshot was persisted.

### Root cause

The run lasted 76 minutes, entirely within one UTC day. The daily reset timer fires at `next_utc_midnight_ns_` (trading_app.cpp:376), which never arrived during this run.

The bootstrap (trading_app.cpp:260–278) **does** set `risk_engine_.set_daily_snapshot(account_.equity())` and writes a snapshot **if** no prior snapshot exists for today. But the journal shows **zero** snapshots, suggesting either:
- The write failed silently, or
- The preflight gate `daily_snapshot_present` (line 163) passed because `risk_engine_.daily_snapshot() > 0` in memory, but the journal write was skipped or errored

### Fix

Add error handling and logging around `journal_.write_snapshot()` at bootstrap (line 266). If the write fails, the preflight should HALT rather than proceed.

### Why this is NOT the cause of the 147 rejects

The handoff hypothesized `SizeClampedToZero` due to `daily_snapshot=0`. But the rejects are all `reason: kill_switch_armed`, which is check #2 in `risk_engine.cpp::evaluate()` — it short-circuits **before** the sizing logic (check #5) ever runs. The daily_snapshot value is irrelevant to these rejects.

---

## Non-bug: range_mr cooldown logic is correct

### Handoff hypothesis

"range_mr is over-firing, emitting on every tick once warmed up, ignoring the 60-tick cooldown."

### Reality

`range_mr.cpp:148` initializes `ticks_since_last_entry_` to `1 << 20` (1,048,576), far above the 60-tick cooldown. It only resets to 0 on line 109 or 113 — **after** an intent is emitted.

Since the kill switch armed at T+4.8s and stayed armed, **zero intents were ever approved**. The strategy never reset its cooldown counter. The 147 rejects are just the strategy emitting one intent per tick (every ~50ms per the main loop sleep) for 11 minutes, all hitting the armed kill switch gate.

The cooldown logic is correct as written. No fix needed.

---

## Cosmetic: log timestamps labeled "Z" but are actually IST

The `agent.jsonl` and `stderr.log` timestamps are labeled with `Z` suffix (UTC) but are actually IST (UTC+5:30). This is a logger config issue, not a functional bug. The journal `at_ns` values are correct (true nanoseconds since epoch).

---

## Recommended fix priority for next demo run

1. **BLOCKER:** Fix Bug 1 (false WS-disconnect trip) — use Option A (update `last_event_ns_` on `Open`).
2. **CRITICAL:** Fix Bug 2 (cancel-all 404) — confirm the correct endpoint and implement a working flatten path.
3. **LOW:** Add error handling for daily snapshot writes at bootstrap.
4. **OPTIONAL:** Fix the logger timezone label.

Once #1 and #2 are fixed, the next demo run should reach LIVE and stay there. The strategy will emit intents, the risk engine will evaluate them properly (no false kill switch), and if a real trip occurs, the flatten will execute.

---

## Appendix: Event counts from journal.db

```
risk_decision_reject: 147  (all reason=kill_switch_armed, detail="ws_disconnect:propr silent for 5012ms")
execution_report:       1  (the cancel-all 404)
kill_switch_trip:       1  (reason="ws" at T+4.8s)
preflight:              1  (probe at bootstrap)
---
Total:                150

intents table:          0 rows (no intents persisted because none were approved)
account_snapshots:      0 rows (no daily reset fired, bootstrap snapshot write may have failed)
strategy_state:         0 rows (strategy has no persistent state)
```

---

## Next steps

1. Apply fixes #1 and #2 above.
2. Run `make test` to confirm no regressions.
3. Start a new demo run on the Free Trial account.
4. Manually trigger an account state change (place a small order via the Propr dashboard) within the first 10 seconds to force a `account.updated` WS message and verify `last_event_ns_` updates correctly.
5. Let it run for at least 2 hours to confirm the kill switch stays disarmed and intents are evaluated properly.
