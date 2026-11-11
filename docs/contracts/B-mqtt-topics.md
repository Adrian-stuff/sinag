# Contract B — MQTT topics & payload (Sinag device → backend)

Broker: Mosquitto on the server laptop, port 1883, no auth (local hotspot only).

## `sinag/{device_id}/telemetry` — every 5 s, QoS 0, not retained

```json
{
  "ts": 1731300000, "pv_w": 3200, "load_w": 1800, "grid_w": 0,
  "batt_soc": 78, "batt_w": -1400, "grid_on": true,
  "inv_temp_c": 42, "fault_code": 0
}
```

- `ts` is Unix seconds from NTP. If the device has no time yet (hotspot with no
  internet), it sends `"ts": 0` — the backend should use its own receive time then.
- Telemetry is only published after a successful Modbus read. If the inverter
  stops answering, telemetry stops; the device stays `online`.

## `sinag/{device_id}/meter` — every 5 s (PROPOSED — needs team-lead approval)

Published only when a smart meter (Contract A2, slave 2) answers. Telemetry and
meter are independent: a site can have either one or both.

```json
{
  "ts": 1731300000, "voltage_v": 230.1, "current_a": 6.52, "power_w": 1450,
  "apparent_va": 1526, "reactive_var": 477, "pf": 0.95, "freq_hz": 60.01,
  "import_kwh": 1520.35, "export_kwh": 12.40, "grid_on": true
}
```

- `power_w`: + importing from the grid, − exporting (net metering).
- `import_kwh` / `export_kwh` are the meter's running totals. The bill is the
  difference between two readings.
- `grid_on` is derived on the device: `voltage_v > 100`.

## `sinag/{device_id}/status` — retained, also the MQTT last-will

Published on every (re)connect and every 30 s:

```json
{ "online": true, "rssi": -55, "fw": "0.1.0" }
```

Last-will (set by the broker when the device drops off):

```json
{ "online": false }
```
