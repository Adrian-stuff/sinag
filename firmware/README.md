# Sinag firmware

| Folder | Board | What it does |
|---|---|---|
| `inverter-sim/` | ESP32 #2 | Simulator with two modes. **Inverter** = hybrid solar inverter, slave 1 (Contract A). **Smart meter** = SDM120-style kWh meter, slave 2 (Contract A2). LCD, 1 knob, 1 button, live web dashboard |
| `sinag-device/` | ESP32 #1 | The connector. Modbus RTU **master** that polls the inverter and the meter every 5 s and publishes each one that answers over MQTT (Contract B) |

## Wiring (no RS485 modules yet: direct UART)

Modbus RTU is just bytes on a serial line. Until the MAX3485 modules arrive, wire
the two ESP32s straight to each other: **TX goes to the other board's RX, and the grounds are shared.**

```
 ESP32 #2  inverter-sim               ESP32 #1  sinag-device
 ──────────────────────               ──────────────────────
 GPIO17 (TX2)  ─────────────────────► GPIO16 (RX2)
 GPIO16 (RX2)  ◄───────────────────── GPIO17 (TX2)
 GND           ────────────────────── GND
```

Keep the wires short (< 1 m). Both boards are 3.3 V, so they need no level shifting.

### Simulator front panel (ESP32 #2)

```
 Potentiometer 5k     left leg → 3V3      (NOT 5V — ESP32 ADC max is 3.3 V)
                      wiper    → GPIO34
                      right leg→ GND

 Push button          one side → GPIO27, other side → GND   (internal pull-up)

 LCD 16x2 + I2C backpack (PCF8574)
                      VCC → VIN/5V     GND → GND
                      SDA → GPIO21     SCL → GPIO22
```

The LCD address is auto-detected (0x27 or 0x3F). If the screen lights up but shows
no text, turn the blue contrast trimmer on the backpack. Most backpacks pull SDA and SCL up
to 5 V. That works with an ESP32 in practice. If you want to be strict, remove the
backpack's pull-up resistors or add a level shifter.

For a 20x4 LCD, set `LCD_COLS 20` and `LCD_ROWS 4` in `inverter-sim/config.h`.
Two extra rows will show then.

### Later: adding RS485 (MAX3485 / TTL-RS485 module) on both boards

```
 ESP32 TX2 (17) → DI      ESP32 RX2 (16) → RO
 ESP32 GPIO4    → DE + RE (tie together)
 Module A ↔ other module A,   B ↔ B,   GND ↔ GND
 120 Ω across A–B at each end if the cable is long
```

Then set `RS485_DE_PIN 4` in **both** `config.h` files. Modules with automatic
direction control (no DE/RE pins) stay at `-1`. If no data flows, try these in order:
check that GND is shared, swap A/B, then swap TX/RX.

## Using the simulator

The simulator has three modes: **inverter**, **meter** (an SDM120 customer submeter on
Modbus) and **MERALCO** (a utility meter like Meralco's Aclara I-210+c, speaking
ANSI C12.18 with C12.19 tables; see `docs/contracts/A3-meralco-meter-c12.md`).
**Hold the button for more than 6 s** to go to the next mode (or use the web page).
It remembers the mode after a reboot. MERALCO mode uses the same knob, button and LCD as
meter mode. The LCD's link page reads `C12 OK` instead of `MB OK`.

| Control | Inverter mode (slave 1) | Smart meter mode (slave 2) |
|---|---|---|
| Turn the knob | Changes the selected target | Changes the selected target |
| Tap the button | Next target: SUN → LOAD → SOC → SPEED | Next target: LOAD → SUN → VOLT → SPEED |
| Hold 1–3 s, release | Toggles **BROWNOUT** (`grid_on=0`, `grid_w=0`) | Toggles **BROWNOUT** (0 V, 0 A, 0 W) |
| Hold 3–6 s, release | Toggles **FAULT 21** (overheating, temperature climbs to ~78 °C) | Toggles **LOW VOLTAGE** (sag to 195 V) |
| Hold > 6 s, release | Next mode (→ meter) | Next mode (meter → MERALCO → inverter) |

What each knob target sets: **SUN** = 0–5000 W solar · **LOAD** = 150–4000 W home use ·
**SOC** = battery 0–100 % · **VOLT** = grid voltage 180–260 V ·
**SPEED** = 1×, 10×, 60×, 300× or 1800× simulated time.

While you hold the button, the LCD shows what releasing it will do. You don't
need the web page for anything. Everything can be done and read from the panel.

**Knob pick-up:** after you switch targets, the new value stays where it was
until you turn the knob a little. This keeps a value from jumping when you switch. The LCD
shows `=` while it waits and `>` once the knob is in control.

**LCD layout.** Row 1 shows what the knob controls. Row 2 cycles every 2.5 s through all the readings:

```
Row 1 (knob)          Row 2 (cycles)
>SUN  60%  3000W      PV 3000 LD 1497W     solar W, home load W
>LOAD 35%  1497W      GRID -1200W EXP      grid W, IMP = buying / EXP = selling
>SOC  78% -1400W      BAT  78% -1400W      battery %, W (− charging, + discharging)
>SPEED    60x         TEMP 41C OK          inverter temp, OK or FAULT21
                      MB OK    123 rq      Modbus: requests answered (or "no polls yet" / "LOST 12s")
                      192.168.43.57        IP for the web page (AP 192.168.4.1 in AP mode)
```

During a brownout or fault, `!! BROWNOUT !!` / `!! FAULT 21 !!` shows in every
other slot on row 2, so it's never hidden. On a 20x4 LCD, rows 3–4 show the next
pages as well.

The battery charges from surplus solar and covers deficits. A 20 % reserve is kept
while the grid is on, and it drains to 5 % during a brownout. Simulated time runs
**60×** by default so you can see the SoC move. Change it with the knob (SPEED) or on the web page.

### Smart meter mode

This mode emulates the meter at a house's service entrance. The house can have
grid-tie solar (no battery). Net power = load − solar: **+ import, − export**
(net metering). The meter reports V, A, W, VA, VAr, PF, 60 Hz and running
**import/export kWh** as float32 registers (SDM120 map, see
`docs/contracts/A2-smart-meter-register-map.md`). The kWh reading is saved to
flash once a minute, so it keeps counting across reboots. It starts at 1520 kWh;
set it with "Set reading…" on the web page. In a brownout, solar shuts off too
(anti-islanding), so everything reads 0.

```
Row 1 (knob)          Row 2 (cycles)
>LOAD 35%  1497W      230.1V   6.52A       voltage, current
>SUN  20%  1000W      P  +497W IMPORT      net power, IMPORT / EXPORT
>VOLT  230.0V         PF 0.95 60.01Hz      power factor, frequency
>SPEED    60x         PV 1000 LD 1497W     solar W, home load W
                      IMP  1520.35kWh      import reading
                      EXP    12.40kWh      export reading
                      MB OK    123 rq      Modbus link
                      192.168.43.57        IP
```

An alert (`!! BROWNOUT !!` / `!LOW VOLTAGE!`) shows every other slot, as in inverter mode.

### Web dashboard

On boot the LCD shows the IP address. Open `http://<that IP>/` or `http://sinag-sim.local/`.
If the hotspot isn't reachable within 15 s, the simulator makes its own Wi-Fi network
**Sinag-Sim** (password `sinag1234`) at `http://192.168.4.1`.

The page has a mode switch at the top (Hybrid inverter / Smart meter). It shows
live values for the current mode, and the active register map is shown decoded and
raw, exactly as the Sinag device reads it. Modbus link stats include the last
request/response bytes. Controls mirror the panel: sun/load/voltage sliders, knob
target, brownout, fault or low voltage, sim speed, and set SoC or set meter
reading. JSON is at `/api/state`. Controls are at `POST /api/control` (`mode`,
`sun`, `load`, `volt`, `knob`, `brownout`, `fault`, `sag`, `speed`, `soc`, `imp`, `exp`).

## Using the Sinag device

1. In `sinag-device/config.h`, set `WIFI_SSID`, `WIFI_PASS`, `MQTT_HOST` (the server laptop IP) and `DEVICE_ID`.
2. Upload it and open Serial Monitor at 115200. Each poll prints the JSON it published:
   ```
   [pub] OK  sinag/sinag-001/telemetry {"ts":1731300000,"pv_w":3000,...}    ← inverter mode
   [pub] OK  sinag/sinag-001/meter {"ts":1731300000,"voltage_v":230.1,...}   ← meter mode
   ```
   The device polls both slave 1 (inverter) and slave 2 (meter). It publishes whichever
   one answers, so you never reflash it when you switch the simulator's mode. A
   device that stops answering is retried every 15 s (`ABSENT_RETRY_MS`), so the
   missing one doesn't slow the other down. After a mode switch, the newly
   active device can take up to ~15 s to show up. To poll only one kind, set
   `INVERTER_ENABLED` or `METER_ENABLED` to 0 in `config.h`.
3. On the laptop:
   ```bash
   mosquitto_sub -h localhost -t "sinag/#" -v
   ```

The device polls Modbus even without Wi-Fi, so you can test the ESP32↔ESP32 link
with just the serial monitor. Read errors print a hint (for example `timeout (no reply: check TX/RX crossed…)`).

Status LED (GPIO2): fast blink = no Wi-Fi · slow blink = no MQTT broker ·
double blink = no device answering · solid = all good (flicks off on each publish).

Real hardware: add a profile in `sinag-device/profiles.h` and point `PROFILE`
(inverter) or `METER_PROFILE` (meter) in `config.h` at it. For an inverter, give the
register, scale and sign of each field. For a meter, give the float32 start register
of each field and its word order. A real Eastron SDM120 works with the built-in
`METER_SDM120` profile once you set it to slave 2 / 9600 baud.

## Build & upload

Libraries: **LiquidCrystal I2C** (sim) · **ModbusMaster**, **PubSubClient**, **ArduinoJson 7** (device).
Board: *ESP32 Dev Module*. Arduino IDE works, or use the CLI:

```bash
arduino-cli lib install "LiquidCrystal I2C" ModbusMaster PubSubClient ArduinoJson
```

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 -u -p /dev/cu.usbserial-XXXX inverter-sim
```

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 -u -p /dev/cu.usbserial-YYYY sinag-device
```

If a board boot-loops with `csum err` / `TG0WDT_SYS_RESET` in the serial log
(common on cheap CH340 boards), its flash can't run at 80 MHz. Build with
`--fqbn "esp32:esp32:esp32:FlashFreq=40,FlashMode=dio"` instead. In the Arduino IDE,
set Tools → Flash Frequency to 40MHz and Flash Mode to DIO.

Done when turning the knob changes the JSON in `mosquitto_sub` within 5 s.
