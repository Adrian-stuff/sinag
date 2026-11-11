# Contract C — REST + WebSocket API (backend → Flutter app)

Base URL: `http://<server-laptop-ip>:8000`. No auth (local hotspot only).
FastAPI serves live docs at `/docs`. Every endpoint below returns the **mock** body
from hour 0, so the app can be built before the data is real.

Conventions
- Time: ISO 8601 in Asia/Manila, e.g. `"2026-11-12T14:05:00+08:00"`.
- Money: pesos as numbers with 2 decimals (`"mtd_bill_php": 1234.50`). Energy: kWh, power: W.
- Missing source → `null` (e.g. `meter: null` when no smart meter is answering).
- Errors: `{"detail": "…"}` with a 4xx/5xx status (FastAPI default).

---

## GET /api/live — Home tab

```json
{
  "ts": "2026-11-12T14:05:00+08:00",
  "online": true,
  "last_seen_s": 3,
  "grid_on": true,
  "inverter": {
    "pv_w": 3200, "load_w": 1800, "grid_w": 0, "batt_soc": 78, "batt_w": -1400,
    "grid_on": true, "inv_temp_c": 42, "fault_code": 0
  },
  "meter": {
    "voltage_v": 230.1, "current_a": 6.52, "power_w": 1450, "pf": 0.95,
    "freq_hz": 60.01, "import_kwh": 1520.35, "export_kwh": 12.40
  },
  "status_line": "Solar is powering your whole home and charging the battery.",
  "saved_today_php": 84.20,
  "alerts_unread": 2
}
```
`inverter` and `meter` are each `null` when that device hasn't reported in the last 30 s.

## WS /ws/live — Home + Alerts badge

Server → client only. One JSON message per event:

```json
{ "type": "live",   "data": { /* same body as GET /api/live */ } }
{ "type": "alert",  "data": { /* one Alert, see /api/alerts */ } }
```
Sent on every MQTT reading (~5 s) and on every new alert. The client reconnects
after 3 s if the socket drops, then calls `GET /api/live` once to catch up.

## GET /api/energy?period=day|week|month — Energy tab

```json
{
  "period": "day",
  "bucket": "hour",
  "points": [
    { "t": "2026-11-12T08:00:00+08:00", "pv_kwh": 1.9, "load_kwh": 0.8,
      "import_kwh": 0.0, "export_kwh": 0.4, "batt_in_kwh": 0.7, "batt_out_kwh": 0.0 }
  ],
  "totals": { "pv_kwh": 18.4, "load_kwh": 14.2, "import_kwh": 2.1, "export_kwh": 3.6 },
  "self_sufficiency_pct": 85
}
```
Buckets: `day` → 24 hourly points, `week` → 7 daily, `month` → one per day so far.
`self_sufficiency_pct = (load − import) / load × 100`.

## GET /api/bill — Bill tab, Home card

```json
{
  "month": "2026-11",
  "rate_php_per_kwh": 12.00,
  "export_credit_php_per_kwh": 6.00,
  "mtd_import_kwh": 96.4,
  "mtd_export_kwh": 41.0,
  "mtd_bill_php": 910.80,
  "forecast_php": { "value": 2240.00, "low": 1980.00, "high": 2510.00 },
  "last_month_php": 2610.00,
  "vs_last_month_pct": -14.2,
  "savings_today_php": 84.20,
  "savings_month_php": 1032.60,
  "weekly": [ { "week_start": "2026-11-03", "cost_php": 512.30 } ]
}
```
- `mtd_bill_php = import × rate − export × export_credit` (floored at 0).
- Savings = solar energy used at home × rate.
- Forecast = month-to-date + average daily cost × days left, ± 1 standard deviation of daily cost.
- kWh comes from the smart meter's import/export counters when a meter exists; otherwise
  from integrating the inverter's `grid_w` over time.

## GET /api/brownout — Brownout advisor

```json
{
  "active": true,
  "since": "2026-11-12T13:40:00+08:00",
  "duration_min": 25,
  "batt_soc": 64,
  "load_w": 900,
  "runtime_h": 7.1,
  "advice": "May 7 oras ka pa ng kuryente. Patayin muna ang aircon para umabot hanggang gabi.",
  "outages_30d": [ { "start": "2026-11-08T19:02:00+08:00", "end": "2026-11-08T20:15:00+08:00", "duration_min": 73 } ]
}
```
`runtime_h = (batt_soc − 5) / 100 × battery_kwh × 1000 / max(load_w, 50)`; `null` when there's no inverter.
When `active` is false, `since`, `duration_min`, `runtime_h` and `advice` are `null`.

## GET /api/alerts?unread_only=false — Alerts tab

```json
[
  { "id": 41, "type": "fault", "severity": "critical", "title": "Inverter overheating (fault 21)",
    "message": "Umiinit ang inverter (78 °C). Tingnan kung may hangin sa paligid nito.",
    "ts": "2026-11-12T13:58:00+08:00", "read": false }
]
```
Newest first, max 100. `type`: `fault` · `brownout` · `grid_back` · `low_batt` · `high_temp` ·
`low_voltage` · `offline` · `online`. `severity`: `info` · `warning` · `critical`.

### POST /api/alerts/{id}/read → `{ "ok": true }`
### POST /api/alerts/read-all → `{ "ok": true, "count": 5 }`

Alert rules (backend, on every reading; each fires once until it clears):

| type | when | severity |
|---|---|---|
| `fault` | `fault_code != 0` | critical |
| `brownout` / `grid_back` | `grid_on` goes false / true (also opens/closes an outage row) | warning / info |
| `low_batt` | `batt_soc < 20` | warning |
| `high_temp` | `inv_temp_c > 60` | warning |
| `low_voltage` | meter `voltage_v < 200` while grid is on | warning |
| `offline` / `online` | status `online:false` or no data for 30 s / data again | warning / info |

## POST /api/chat — Ask Sinag

Request:
```json
{ "message": "Bakit mataas ang bill ko?", "history": [ { "role": "user", "content": "…" }, { "role": "assistant", "content": "…" } ] }
```
Response:
```json
{
  "reply": "Mas mataas ng 14% ang gamit mo sa grid ngayong linggo, karamihan 7–10 PM...",
  "chips": [ { "label": "Bill so far", "value": "₱910.80" }, { "label": "Peak hour", "value": "8 PM" } ],
  "source": "llm",
  "latency_ms": 6200
}
```
`source` is `"template"` when the LLM timed out (12 s) and a facts-only answer was used.
`history` is optional, max the last 6 turns.

**PROPOSED (grounded tool calling, needs the project lead's approval):** three more response fields, and a third `source` value.

```json
{
  "tools_used": [ { "tool": "get_bill", "args": { "month": "this_month" }, "label": "Bill this month" } ],
  "grounded": true,
  "facts": { "bill.mtd_php": "₱910.80", "bill.forecast_php": "₱2,240.00" }
}
```
- `source`: `"llm"` (passed the number check) · `"template"` (check failed or timeout) · `"no_data"` (no tool fits).
- `grounded`: true unless `source` is `"no_data"`.
- App: show `tools_used[].label` as "Batay sa:" chips; tap → sheet listing `facts`.

### GET /api/chat/log?limit=20 — debug only (PROPOSED)
Each turn's question, router plan, facts, raw model text and check result. Not used by the app.

## GET /api/summary/daily?date=2026-11-11 — Home card

```json
{ "date": "2026-11-11", "text": "Kahapon, 85% ng kuryente mo galing sa araw…", "generated_at": "2026-11-12T00:05:00+08:00", "source": "llm" }
```
Pre-generated after midnight (and on demand if missing). Defaults to yesterday.

## GET /api/settings · PUT /api/settings — Settings

```json
{
  "user_name": "Juan",
  "device_id": "sinag-001",
  "rate_php_per_kwh": 12.00,
  "export_credit_php_per_kwh": 6.00,
  "battery_kwh": 10.0
}
```
PUT takes any subset of the fields and returns the full object.
Default rates are placeholders: set the current Meralco rate on the day.

## GET /api/health — setup / debugging

```json
{ "mqtt": true, "db": true, "llm": true, "llm_model": "qwen2.5:3b", "device_online": true, "last_reading_age_s": 3 }
```
