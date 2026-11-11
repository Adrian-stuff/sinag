// =============================================================================
//  Sinag — Simulator (ESP32 #2). Two personalities, one box:
//
//    INVERTER mode  Hybrid solar inverter, Modbus RTU slave ID 1, Contract A map
//    METER mode     Smart kWh meter (Eastron SDM120-style), slave ID 2,
//                   float32 registers: V, A, W, VA, VAr, PF, Hz, import/export kWh
//
//  Front panel (1 knob, 1 button, LCD)
//    Knob ........ sets the selected target
//                    inverter: SUN · LOAD · SOC · SPEED
//                    meter:    LOAD · SUN · VOLT · SPEED
//    Tap ......... next knob target
//    Hold 1–3 s .. toggle BROWNOUT
//    Hold 3–6 s .. inverter: toggle FAULT 21 · meter: toggle LOW VOLTAGE
//    Hold > 6 s .. switch INVERTER <-> METER mode (remembered after reboot)
//
//  LCD row 1 = what the knob controls. Row 2 cycles every 2.5 s through all the
//  readings; an active alert (brownout, fault, low voltage) shows every other slot.
//
//  The knob uses "pick-up": after switching, the new value stays put until you
//  turn the knob a little — so switching never makes a value jump.
// =============================================================================
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <Preferences.h>
#include <LiquidCrystal_I2C.h>
#include <stdarg.h>
#include "config.h"
#include "web_page.h"

#define FW_VERSION      "0.2.0"
#define NUM_REGS        8          // inverter map: 0..7
#define METER_NUM_REGS  0x15A      // meter map: up to total kWh at 0x0156

// ----------------------------------------------------------------- modes ----
enum Mode : uint8_t { MODE_INVERTER, MODE_METER };
static const char *const MODE_IDS[] = { "inverter", "meter" };

enum Knob : uint8_t { KNOB_SUN, KNOB_LOAD, KNOB_SOC, KNOB_SPEED, KNOB_VOLT, KNOB_COUNT };
static const char *const KNOB_IDS[KNOB_COUNT]   = { "sun", "load", "soc", "speed", "volt" };
static const char *const KNOB_NAMES[KNOB_COUNT] = { "SUN", "LOAD", "BATTERY SOC", "SIM SPEED", "VOLTAGE" };
#define KNOBS_PER_MODE 4
static const Knob KNOBS_INV[KNOBS_PER_MODE] = { KNOB_SUN, KNOB_LOAD, KNOB_SOC, KNOB_SPEED };
static const Knob KNOBS_MTR[KNOBS_PER_MODE] = { KNOB_LOAD, KNOB_SUN, KNOB_VOLT, KNOB_SPEED };
static const int  SPEED_STEPS[] = { 1, 10, 60, 300, 1800 };
static const int  NUM_SPEEDS = sizeof SPEED_STEPS / sizeof SPEED_STEPS[0];

// LCD row-2 data pages (declared up here so Arduino's auto-prototypes can see the type)
enum Page : uint8_t { PG_POWER, PG_GRID, PG_BATT, PG_TEMP, PG_LINK, PG_IP, PG_M_VI, PG_M_P, PG_M_PFHZ, PG_M_IMP, PG_M_EXP };
static const Page PAGES_INV[] = { PG_POWER, PG_GRID, PG_BATT, PG_TEMP, PG_LINK, PG_IP };
static const Page PAGES_MTR[] = { PG_M_VI, PG_M_P, PG_M_PFHZ, PG_POWER, PG_M_IMP, PG_M_EXP, PG_LINK, PG_IP };

// ----------------------------------------------------------------- state ----
struct SimState {
  Mode  mode     = MODE_INVERTER;
  int   sunPct   = 60;
  int   loadPct  = 35;
  bool  brownout = false;
  bool  fault    = false;      // inverter: fault 21
  bool  sag      = false;      // meter: low voltage
  int   speed    = DEFAULT_SPEED;
  float soc      = 78.0f;
  float temp     = 34.0f;
  float voltSet  = METER_V_NOMINAL;
  // shared outputs
  int   pvW = 0, loadW = 0, gridW = 0, battW = 0;
  // meter outputs
  float  mV = 0, mI = 0, mP = 0, mVA = 0, mVAR = 0, mPF = 1, mHz = 0;
  double impKwh = METER_START_KWH, expKwh = 0;
} S;

uint16_t regs[NUM_REGS];
uint16_t mregs[METER_NUM_REGS];

// SDM120 input-register map (float32, high word first)
struct MeterReg { uint16_t addr; const char *name; const char *unit; uint8_t dec; };
static const MeterReg METER_MAP[] = {
  { 0x0000, "voltage",        "V",   1 },
  { 0x0006, "current",        "A",   2 },
  { 0x000C, "active_power",   "W",   0 },
  { 0x0012, "apparent_power", "VA",  0 },
  { 0x0018, "reactive_power", "VAr", 0 },
  { 0x001E, "power_factor",   "",    2 },
  { 0x0046, "frequency",      "Hz",  2 },
  { 0x0048, "import_kwh",     "kWh", 2 },
  { 0x004A, "export_kwh",     "kWh", 2 },
  { 0x0156, "total_kwh",      "kWh", 2 },
};

static const char *const INV_FIELDS[NUM_REGS] = { "pv_w", "load_w", "grid_w", "batt_soc", "batt_w", "grid_on", "inv_temp_c", "fault_code" };
static const char *const INV_UNITS[NUM_REGS]  = { "W", "W", "W", "%", "W", "", "°C", "" };

Knob     knob        = KNOB_SUN;
bool     knobLocked  = true;     // waiting for the knob to move before it takes over
float    knobRef     = 0;        // pot position when it got locked
float    potEma      = -1;       // smoothed pot, 0..100
int      potPct      = 0;
int      lastAppliedPot = -1;    // knob only writes when it moves (so SOC can still drift)

Preferences prefs;

static bool isMeter() { return S.mode == MODE_METER; }
static const Knob *modeKnobs() { return isMeter() ? KNOBS_MTR : KNOBS_INV; }
static int knobIndex(Knob k) {
  for (int i = 0; i < KNOBS_PER_MODE; i++) if (modeKnobs()[i] == k) return i;
  return -1;
}

// --------------------------------------------------------------- modbus ----
HardwareSerial &MB = Serial2;

struct MbStats {
  uint32_t requests = 0, crcErrors = 0, otherId = 0, exceptions = 0;
  uint32_t lastReqMs = 0;
  char     lastRx[3 * 16 + 1] = "";
  char     lastTx[3 * 24 + 1] = "";
} mb;

uint8_t  rxBuf[64];
size_t   rxLen = 0;
uint32_t lastByteMs = 0;

static uint8_t  activeSlaveId() { return isMeter() ? METER_SLAVE_ID : MB_SLAVE_ID; }
static uint16_t activeRegCount() { return isMeter() ? METER_NUM_REGS : NUM_REGS; }
static uint16_t activeReg(uint16_t a) { return isMeter() ? mregs[a] : regs[a]; }

static uint16_t crc16(const uint8_t *d, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    crc ^= d[i];
    for (int b = 0; b < 8; b++) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
  }
  return crc;
}

static void toHex(char *out, size_t outSize, const uint8_t *d, size_t n) {
  size_t p = 0;
  out[0] = 0;
  for (size_t i = 0; i < n && p + 4 <= outSize; i++) p += snprintf(out + p, outSize - p, i ? " %02X" : "%02X", d[i]);
}

static void mbSend(uint8_t *buf, size_t len) {
  uint16_t crc = crc16(buf, len);
  buf[len++] = crc & 0xFF;
  buf[len++] = crc >> 8;
  toHex(mb.lastTx, sizeof mb.lastTx, buf, len);
  if (RS485_DE_PIN >= 0) digitalWrite(RS485_DE_PIN, HIGH);
  MB.write(buf, len);
  MB.flush();                                   // wait until the last bit is out
  if (RS485_DE_PIN >= 0) digitalWrite(RS485_DE_PIN, LOW);
}

static void mbException(uint8_t fc, uint8_t code) {
  uint8_t out[5] = { activeSlaveId(), (uint8_t)(fc | 0x80), code };
  mb.exceptions++;
  mbSend(out, 3);
}

static void mbHandleFrame(const uint8_t *f, size_t n) {
  if (n < 4) return;
  uint16_t crc = crc16(f, n - 2);
  if ((f[n - 2] | (f[n - 1] << 8)) != crc) { mb.crcErrors++; return; }
  if (f[0] != activeSlaveId()) { mb.otherId++; return; }   // not for us (or broadcast)

  mb.requests++;
  mb.lastReqMs = millis();
  toHex(mb.lastRx, sizeof mb.lastRx, f, n);

  uint8_t fc = f[1];
  if (fc == 0x03 || fc == 0x04) {               // both answered from the same map
    uint16_t addr = (f[2] << 8) | f[3];
    uint16_t qty  = (f[4] << 8) | f[5];
    if (qty == 0 || qty > 125)                     return mbException(fc, 0x03);
    if ((uint32_t)addr + qty > activeRegCount())   return mbException(fc, 0x02);
    static uint8_t out[3 + 2 * 125 + 2];
    out[0] = activeSlaveId(); out[1] = fc; out[2] = (uint8_t)(qty * 2);
    for (uint16_t i = 0; i < qty; i++) {
      uint16_t v = activeReg(addr + i);
      out[3 + 2 * i]     = v >> 8;
      out[3 + 2 * i + 1] = v & 0xFF;
    }
    return mbSend(out, 3 + 2 * qty);
  }
  mbException(fc, 0x01);                        // registers are read-only
}

// Expected request length from the header, 0 if unknown yet.
static size_t mbExpectedLen() {
  if (rxLen < 2) return 0;
  switch (rxBuf[1]) {
    case 0x03: case 0x04: case 0x05: case 0x06: return 8;
    case 0x0F: case 0x10: return rxLen >= 7 ? 9 + rxBuf[6] : 0;
    default: return 0;
  }
}

static void mbPoll() {
  while (MB.available()) {
    int c = MB.read();
    if (rxLen >= sizeof rxBuf) rxLen = 0;      // overflow -> resync
    rxBuf[rxLen++] = (uint8_t)c;
    lastByteMs = millis();
  }
  if (!rxLen) return;

  size_t need = mbExpectedLen();
  if (need && need <= sizeof rxBuf && rxLen >= need) {
    mbHandleFrame(rxBuf, need);
    rxLen = 0;                                  // master waits for our reply, so nothing else is queued
    return;
  }
  if (millis() - lastByteMs > 20) {             // silence: frame ended (or garbage)
    mbHandleFrame(rxBuf, rxLen);
    rxLen = 0;
  }
}

// -------------------------------------------------------------- physics ----
static int knobLoadW() { return LOAD_MIN_W + (long)S.loadPct * (LOAD_MAX_W - LOAD_MIN_W) / 100; }
static int knobPvW()   { return (long)S.sunPct * PV_MAX_W / 100; }

static void putFloat(uint16_t addr, float v) {
  uint32_t u;
  memcpy(&u, &v, 4);
  mregs[addr]     = u >> 16;          // high word first (SDM convention)
  mregs[addr + 1] = u & 0xFFFF;
}
static float getFloat(uint16_t addr) {
  uint32_t u = ((uint32_t)mregs[addr] << 16) | mregs[addr + 1];
  float v;
  memcpy(&v, &u, 4);
  return v;
}

static void updateInverterRegisters() {
  regs[0] = (uint16_t)S.pvW;
  regs[1] = (uint16_t)S.loadW;
  regs[2] = (uint16_t)(int16_t)S.gridW;
  regs[3] = (uint16_t)lroundf(S.soc);
  regs[4] = (uint16_t)(int16_t)S.battW;
  regs[5] = S.brownout ? 0 : 1;
  regs[6] = (uint16_t)(int16_t)lroundf(S.temp);
  regs[7] = S.fault ? FAULT_CODE : 0;
}

static void inverterTick(float dtReal) {
  const bool gridOn = !S.brownout;
  const int  pvAvail = knobPvW();
  int load = knobLoadW();
  int pv = pvAvail, batt = 0, grid = 0;

  int net = pvAvail - load;
  if (net >= 0) {                                       // surplus: charge, then export
    int chg = S.soc < 100.0f ? min(net, BATT_MAX_CHG_W) : 0;
    if (S.soc > 95.0f) chg = (int)(chg * (100.0f - S.soc) / 5.0f);   // taper near full
    batt = -chg;
    int rest = net - chg;
    if (gridOn) grid = -rest;                           // export to grid
    else        pv   = load + chg;                      // brownout: curtail solar
  } else {                                              // deficit: battery, then grid
    int need = -net;
    int minSoc = gridOn ? BATT_RESERVE : BATT_MIN_SOC;
    int dis = S.soc > minSoc ? min(need, BATT_MAX_DIS_W) : 0;
    batt = dis;
    int rest = need - dis;
    if (gridOn) grid = rest;
    else        load = pv + dis;                        // brownout: what we can't cover goes dark
  }

  float dtSim = dtReal * S.speed;                       // sim time runs `speed` times faster
  S.soc -= (float)batt * dtSim / 3600.0f / (BATT_KWH * 1000.0f) * 100.0f;
  S.soc = constrain(S.soc, 0.0f, 100.0f);

  // inverter temperature (real-time, so the fault demo is visible in ~20 s)
  float target = S.fault ? 78.0f : 32.0f + 14.0f * (float)(pv + abs(batt)) / (PV_MAX_W + BATT_MAX_DIS_W);
  S.temp += (target - S.temp) * min(1.0f, 0.12f * dtReal);

  S.pvW = pv; S.loadW = load; S.gridW = grid; S.battW = batt;
  updateInverterRegisters();
}

// Smart meter at the service entrance of a house with optional grid-tie solar
// (no battery). Net power = load - solar: + import, - export (net metering).
static void meterTick(float dtReal) {
  const bool on = !S.brownout;
  int load = on ? knobLoadW() : 0;
  int pv   = on ? knobPvW()   : 0;            // grid-tie solar shuts off in a brownout (anti-islanding)
  float p  = (float)(load - pv);

  float v  = on ? (S.sag ? METER_SAG_V : S.voltSet) + random(-60, 61) / 100.0f : 0.0f;
  float hz = on ? METER_HZ + random(-3, 4) / 100.0f : 0.0f;
  float pf = fabsf(p) < 1.0f ? 1.0f : (p >= 0 ? METER_PF : -METER_PF);   // sign follows power direction (SDM style)
  float va  = fabsf(p) / fabsf(pf);
  float var = sqrtf(max(0.0f, va * va - p * p));
  float i   = v > 1.0f ? va / v : 0.0f;

  double kwh = (double)fabsf(p) * dtReal * S.speed / 3.6e6;
  if (p > 0) S.impKwh += kwh; else S.expKwh += kwh;

  S.mV = v; S.mI = i; S.mP = p; S.mVA = va; S.mVAR = var; S.mPF = pf; S.mHz = hz;
  S.pvW = pv; S.loadW = load; S.gridW = (int)p; S.battW = 0;

  putFloat(0x0000, v);
  putFloat(0x0006, i);
  putFloat(0x000C, p);
  putFloat(0x0012, va);
  putFloat(0x0018, p >= 0 ? var : -var);
  putFloat(0x001E, pf);
  putFloat(0x0046, hz);
  putFloat(0x0048, (float)S.impKwh);
  putFloat(0x004A, (float)S.expKwh);
  putFloat(0x0156, (float)(S.impKwh + S.expKwh));
}

static void simTick(float dtReal) { if (isMeter()) meterTick(dtReal); else inverterTick(dtReal); }

// Persist the meter reading so it keeps counting across reboots (once a minute).
static void saveMeterReading(bool force) {
  static double savedImp = -1, savedExp = -1;
  if (!force && fabs(S.impKwh - savedImp) < 0.005 && fabs(S.expKwh - savedExp) < 0.005) return;
  prefs.putDouble("imp", S.impKwh);
  prefs.putDouble("exp", S.expKwh);
  savedImp = S.impKwh; savedExp = S.expKwh;
}

// ------------------------------------------------------------ knob/button --
static void applyKnob(int pct) {
  switch (knob) {
    case KNOB_SUN:   S.sunPct  = pct; break;
    case KNOB_LOAD:  S.loadPct = pct; break;
    case KNOB_SOC:   S.soc     = pct; break;
    case KNOB_SPEED: S.speed   = SPEED_STEPS[min(NUM_SPEEDS - 1, pct * NUM_SPEEDS / 100)]; break;
    case KNOB_VOLT:  S.voltSet = METER_V_MIN + pct * (METER_V_MAX - METER_V_MIN) / 100.0f; break;
    default: break;
  }
}

static void lockKnob() { knobLocked = true; knobRef = potEma; lastAppliedPot = -1; }

static void readPot() {
  int raw = analogRead(POT_PIN);
  float pct = constrain((raw - 40) * 100.0f / (4050 - 40), 0.0f, 100.0f);
  potEma = potEma < 0 ? pct : potEma + 0.25f * (pct - potEma);
  if (fabsf(potEma - potPct) >= 0.7f) potPct = (int)lroundf(potEma);   // hysteresis

  if (knobLocked && fabsf(potEma - knobRef) >= 4.0f) knobLocked = false;
  if (!knobLocked && potPct != lastAppliedPot) { lastAppliedPot = potPct; applyKnob(potPct); }
}

// LCD overlay message (shown for a moment after an event)
char     msg0[21] = "", msg1[21] = "";
uint32_t msgUntil = 0;
static void flash(const char *a, const char *b, uint32_t ms = 1500) {
  strlcpy(msg0, a, sizeof msg0); strlcpy(msg1, b, sizeof msg1);
  msgUntil = millis() + ms;
}

static void setBrownout(bool on) { S.brownout = on; flash("BROWNOUT", on ? "ON  (grid off)" : "OFF (grid back)"); }
static void setFault(bool on)    { S.fault = on;    flash(on ? "FAULT 21" : "FAULT", on ? "overheating" : "cleared"); }
static void setSag(bool on)      { S.sag = on;      flash("LOW VOLTAGE", on ? "ON  (195 V)" : "OFF"); }
static void setKnob(Knob k)      { knob = k; lockKnob(); flash("Knob controls", KNOB_NAMES[k], 900); }

static void setMode(Mode m) {
  if (m == S.mode) return;
  S.mode = m;
  if (knobIndex(knob) < 0) knob = modeKnobs()[0];
  lockKnob();
  rxLen = 0;
  prefs.putUChar("mode", m);
  char b[21];
  snprintf(b, sizeof b, "Modbus ID %d", activeSlaveId());
  flash(isMeter() ? "MODE: METER" : "MODE: INVERTER", b, 2500);
  Serial.printf("[mode] %s, slave id %d\n", MODE_IDS[m], activeSlaveId());
  simTick(0);
}

bool     btnDown = false, btnStable = false;
uint32_t btnChangedMs = 0, btnPressedMs = 0;

static void readButton() {
  bool down = digitalRead(BUTTON_PIN) == LOW;
  uint32_t now = millis();
  if (down != btnDown) { btnDown = down; btnChangedMs = now; }
  if (now - btnChangedMs < 30 || down == btnStable) return;      // debounce
  btnStable = down;
  if (down) { btnPressedMs = now; return; }
  uint32_t held = now - btnPressedMs;
  if (held >= MODE_PRESS_MS)            setMode(isMeter() ? MODE_INVERTER : MODE_METER);
  else if (held >= VERY_LONG_PRESS_MS)  { if (isMeter()) setSag(!S.sag); else setFault(!S.fault); }
  else if (held >= LONG_PRESS_MS)       setBrownout(!S.brownout);
  else                                  setKnob(modeKnobs()[(knobIndex(knob) + 1) % KNOBS_PER_MODE]);
}

// -------------------------------------------------------------------- LCD --
LiquidCrystal_I2C *lcd = nullptr;
char lcdCache[4][21];

static uint8_t findLcd() {
  if (LCD_ADDR) return LCD_ADDR;
  const uint8_t tryFirst[] = { 0x27, 0x3F };
  for (uint8_t a : tryFirst) { Wire.beginTransmission(a); if (Wire.endTransmission() == 0) return a; }
  for (uint8_t a = 0x20; a <= 0x3F; a++) { Wire.beginTransmission(a); if (Wire.endTransmission() == 0) return a; }
  return 0;
}

static void lcdLine(uint8_t row, const char *text) {
  if (!lcd || row >= LCD_ROWS) return;
  char buf[21];
  snprintf(buf, sizeof buf, "%-*.*s", LCD_COLS, LCD_COLS, text);
  if (strcmp(buf, lcdCache[row]) == 0) return;      // only redraw what changed
  strcpy(lcdCache[row], buf);
  lcd->setCursor(0, row);
  lcd->print(buf);
}

static bool modbusLinked() { return mb.lastReqMs && millis() - mb.lastReqMs < 7000; }

// Row 1: what the knob controls. '>' = knob in control, '=' = turn it to take over.
static void knobLine(char *out, size_t n) {
  char c = knobLocked ? '=' : '>';
  switch (knob) {
    case KNOB_SUN:   snprintf(out, n, "%cSUN %3d%% %5dW", c, S.sunPct, S.pvW); break;
    case KNOB_LOAD:  snprintf(out, n, "%cLOAD%3d%% %5dW", c, S.loadPct, S.loadW); break;
    case KNOB_SOC:   snprintf(out, n, "%cSOC %3d%% %+5dW", c, (int)lroundf(S.soc), S.battW); break;
    case KNOB_SPEED: snprintf(out, n, "%cSPEED %5dx", c, S.speed); break;
    case KNOB_VOLT:  snprintf(out, n, "%cVOLT %6.1fV", c, S.voltSet); break;
    default: out[0] = 0;
  }
}

// Row 2: data pages, cycling.
#define PAGE_MS 2500

static void pageText(Page pg, char *out, size_t n) {
  switch (pg) {
    case PG_POWER: snprintf(out, n, "PV%5d LD%5dW", S.pvW, S.loadW); break;
    case PG_GRID:
      if (S.brownout) snprintf(out, n, "GRID OFF (BRN)");
      else snprintf(out, n, "GRID%+6dW %s", S.gridW, S.gridW > 0 ? "IMP" : S.gridW < 0 ? "EXP" : "");
      break;
    case PG_BATT:  snprintf(out, n, "BAT%4d%%%+6dW", (int)lroundf(S.soc), S.battW); break;
    case PG_TEMP:  snprintf(out, n, "TEMP %2dC %s", (int)lroundf(S.temp), S.fault ? "FAULT21" : "OK"); break;
    case PG_LINK:
      if (!mb.lastReqMs)       snprintf(out, n, "MB no polls yet");
      else if (modbusLinked()) snprintf(out, n, "MB OK %6lu rq", (unsigned long)mb.requests);
      else                     snprintf(out, n, "MB LOST %4lus", (unsigned long)((millis() - mb.lastReqMs) / 1000));
      break;
    case PG_IP: {
      bool ap = WiFi.getMode() == WIFI_AP;
      IPAddress ip = ap ? WiFi.softAPIP() : WiFi.localIP();
      if (!ap && WiFi.status() != WL_CONNECTED) snprintf(out, n, "WiFi connecting");
      else snprintf(out, n, "%s%s", ap ? "AP " : "", ip.toString().c_str());   // IP alone can be 15 chars
      break;
    }
    case PG_M_VI:   snprintf(out, n, "%5.1fV %6.2fA", S.mV, S.mI); break;
    case PG_M_P:    snprintf(out, n, "P%+6dW %s", (int)lroundf(S.mP), S.mP > 0.5f ? "IMPORT" : S.mP < -0.5f ? "EXPORT" : ""); break;
    case PG_M_PFHZ: snprintf(out, n, "PF%5.2f %5.2fHz", S.mPF, S.mHz); break;
    case PG_M_IMP:  snprintf(out, n, "IMP%9.2fkWh", S.impKwh); break;
    case PG_M_EXP:  snprintf(out, n, "EXP%9.2fkWh", S.expKwh); break;
    default: out[0] = 0;
  }
}

static bool alertActive() { return S.brownout || (isMeter() ? S.sag : S.fault); }

static void alertText(char *out, size_t n) {
  if (isMeter()) snprintf(out, n, S.brownout ? "!! BROWNOUT !!" : "!LOW VOLTAGE!");
  else if (S.brownout && S.fault) snprintf(out, n, "!BROWNOUT+F21!");
  else snprintf(out, n, S.brownout ? "!! BROWNOUT !!" : "!! FAULT 21 !!");
}

static void drawLcd() {
  char a[24], b[24];
  uint32_t now = millis();
  uint32_t slot = now / PAGE_MS;
  bool alert = alertActive();
  const Page *pages = isMeter() ? PAGES_MTR : PAGES_INV;
  uint8_t numPages  = isMeter() ? sizeof PAGES_MTR / sizeof PAGES_MTR[0] : sizeof PAGES_INV / sizeof PAGES_INV[0];
  uint8_t page = alert ? (slot / 2) % numPages : slot % numPages;

  if (btnStable && now - btnPressedMs >= LONG_PRESS_MS) {       // hold hint
    uint32_t held = now - btnPressedMs;
    snprintf(a, sizeof a, "Release to");
    if (held >= MODE_PRESS_MS)           snprintf(b, sizeof b, "MODE: %s", isMeter() ? "INVERTER" : "METER");
    else if (held >= VERY_LONG_PRESS_MS) snprintf(b, sizeof b, isMeter() ? "LOW VOLT %s" : "FAULT %s",
                                                  (isMeter() ? S.sag : S.fault) ? "OFF" : "ON");
    else                                 snprintf(b, sizeof b, "BROWNOUT %s", S.brownout ? "OFF" : "ON");
  } else if (now < msgUntil) {
    strcpy(a, msg0); strcpy(b, msg1);
  } else {
    knobLine(a, sizeof a);
    if (alert && slot % 2 == 0) alertText(b, sizeof b);     // alert every other slot
    else                        pageText(pages[page], b, sizeof b);
  }
  lcdLine(0, a);
  lcdLine(1, b);

  // Bigger LCD (20x4): extra rows show the next pages too.
  for (uint8_t r = 2; r < LCD_ROWS && r < 4; r++) {
    char c[24];
    pageText(pages[(page + r - 1) % numPages], c, sizeof c);
    lcdLine(r, c);
  }
}

// ------------------------------------------------------------------- web ---
WebServer web(80);

static char   jbuf[3072];
static size_t jlen;
static void jcat(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(jbuf + jlen, sizeof jbuf - jlen, fmt, ap);
  va_end(ap);
  if (n > 0) jlen = min(sizeof jbuf - 1, jlen + (size_t)n);
}

static void sendState() {
  jlen = 0;
  bool m = isMeter();
  jcat("{\"mode\":\"%s\",", MODE_IDS[S.mode]);
  jcat("\"inv\":{\"pv_w\":%d,\"load_w\":%d,\"grid_w\":%d,\"batt_soc\":%d,\"batt_w\":%d,\"grid_on\":%d,\"inv_temp_c\":%d,\"fault_code\":%d},",
       S.pvW, S.loadW, S.gridW, (int)lroundf(S.soc), S.battW, S.brownout ? 0 : 1, (int)lroundf(S.temp), S.fault ? FAULT_CODE : 0);
  jcat("\"meter\":{\"v\":%.1f,\"i\":%.2f,\"p\":%.0f,\"va\":%.0f,\"var\":%.0f,\"pf\":%.2f,\"hz\":%.2f,\"imp\":%.2f,\"exp\":%.2f,\"load_w\":%d,\"pv_w\":%d},",
       S.mV, S.mI, S.mP, S.mVA, S.mVAR, S.mPF, S.mHz, S.impKwh, S.expKwh, S.loadW, S.pvW);

  // register table of the active map, exactly as the Sinag device reads it
  jcat("\"table\":[");
  if (m) {
    for (size_t i = 0; i < sizeof METER_MAP / sizeof METER_MAP[0]; i++) {
      const MeterReg &r = METER_MAP[i];
      jcat("%s[\"0x%04X\",\"%s\",\"%.*f %s\",\"%04X %04X\"]", i ? "," : "", r.addr, r.name, r.dec,
           getFloat(r.addr), r.unit, mregs[r.addr], mregs[r.addr + 1]);
    }
  } else {
    int vals[NUM_REGS] = { S.pvW, S.loadW, S.gridW, regs[3], S.battW, regs[5], (int16_t)regs[6], regs[7] };
    for (int i = 0; i < NUM_REGS; i++)
      jcat("%s[\"%d\",\"%s\",\"%d %s\",\"%u / 0x%04X\"]", i ? "," : "", i, INV_FIELDS[i], vals[i], INV_UNITS[i], regs[i], regs[i]);
  }
  jcat("],");

  jcat("\"ctl\":{\"sun\":%d,\"load\":%d,\"brownout\":%s,\"fault\":%s,\"sag\":%s,\"speed\":%d,\"volt\":%.1f,"
       "\"knob\":\"%s\",\"knob_locked\":%s,\"pot\":%d,\"soc\":%.2f,\"knobs\":[",
       S.sunPct, S.loadPct, S.brownout ? "true" : "false", S.fault ? "true" : "false", S.sag ? "true" : "false",
       S.speed, S.voltSet, KNOB_IDS[knob], knobLocked ? "true" : "false", potPct, S.soc);
  for (int i = 0; i < KNOBS_PER_MODE; i++) jcat("%s\"%s\"", i ? "," : "", KNOB_IDS[modeKnobs()[i]]);
  jcat("]},");

  uint32_t age = mb.lastReqMs ? (millis() - mb.lastReqMs) / 1000 : 0;
  jcat("\"mb\":{\"linked\":%s,\"requests\":%lu,\"crc_errors\":%lu,\"other_id\":%lu,\"exceptions\":%lu,"
       "\"last_age_s\":%lu,\"never\":%s,\"last_rx\":\"%s\",\"last_tx\":\"%s\",\"slave_id\":%d,\"baud\":%d,\"rs485\":%s},",
       modbusLinked() ? "true" : "false", (unsigned long)mb.requests, (unsigned long)mb.crcErrors,
       (unsigned long)mb.otherId, (unsigned long)mb.exceptions, (unsigned long)age,
       mb.lastReqMs ? "false" : "true", mb.lastRx, mb.lastTx, activeSlaveId(), MB_BAUD,
       RS485_DE_PIN >= 0 ? "true" : "false");

  bool ap = WiFi.getMode() == WIFI_AP;
  jcat("\"sys\":{\"fw\":\"%s\",\"uptime_s\":%lu,\"rssi\":%d,\"ip\":\"%s\",\"ap\":%s}}",
       FW_VERSION, (unsigned long)(millis() / 1000), ap ? 0 : (int)WiFi.RSSI(),
       (ap ? WiFi.softAPIP() : WiFi.localIP()).toString().c_str(), ap ? "true" : "false");

  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json", jbuf);
}

static void handleControl() {
  if (web.hasArg("mode")) setMode(web.arg("mode") == "meter" ? MODE_METER : MODE_INVERTER);
  if (web.hasArg("knob"))
    for (uint8_t k = 0; k < KNOB_COUNT; k++)
      if (web.arg("knob") == KNOB_IDS[k] && knobIndex((Knob)k) >= 0) setKnob((Knob)k);
  if (web.hasArg("sun"))   { S.sunPct  = constrain(web.arg("sun").toInt(), 0, 100);  if (knob == KNOB_SUN)  lockKnob(); }
  if (web.hasArg("load"))  { S.loadPct = constrain(web.arg("load").toInt(), 0, 100); if (knob == KNOB_LOAD) lockKnob(); }
  if (web.hasArg("volt"))  { S.voltSet = constrain(web.arg("volt").toFloat(), (float)METER_V_MIN, (float)METER_V_MAX); if (knob == KNOB_VOLT) lockKnob(); }
  if (web.hasArg("brownout")) { bool v = web.arg("brownout") == "1"; if (v != S.brownout) setBrownout(v); }
  if (web.hasArg("fault"))    { bool v = web.arg("fault") == "1";    if (v != S.fault)    setFault(v); }
  if (web.hasArg("sag"))      { bool v = web.arg("sag") == "1";      if (v != S.sag)      setSag(v); }
  if (web.hasArg("speed")) { S.speed = constrain(web.arg("speed").toInt(), 1, 3600);        if (knob == KNOB_SPEED) lockKnob(); }
  if (web.hasArg("soc"))   { S.soc   = constrain(web.arg("soc").toFloat(), 0.0f, 100.0f); if (knob == KNOB_SOC)   lockKnob(); }
  if (web.hasArg("imp"))   { S.impKwh = max(0.0, web.arg("imp").toDouble()); saveMeterReading(true); }
  if (web.hasArg("exp"))   { S.expKwh = max(0.0, web.arg("exp").toDouble()); saveMeterReading(true); }
  simTick(0);                                    // reflect the change in the registers right away
  sendState();
}

// ------------------------------------------------------------------ wifi ---
bool     apMode = false;
bool     mdnsUp = false;
uint32_t wifiStartMs = 0;

static void startAp() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  apMode = true;
  Serial.printf("[wifi] AP mode: join \"%s\" / \"%s\" -> http://%s\n", AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
  flash("AP " AP_SSID, WiFi.softAPIP().toString().c_str(), 6000);
}

static void wifiLoop() {
  if (apMode) {
    if (!mdnsUp) { mdnsUp = MDNS.begin(MDNS_NAME); if (mdnsUp) MDNS.addService("http", "tcp", 80); }
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    if (!mdnsUp) {
      mdnsUp = MDNS.begin(MDNS_NAME);
      if (mdnsUp) MDNS.addService("http", "tcp", 80);
      Serial.printf("[wifi] connected, open http://%s  (or http://%s.local)\n", WiFi.localIP().toString().c_str(), MDNS_NAME);
      flash("Web dashboard:", WiFi.localIP().toString().c_str(), 6000);
    }
  } else if (!mdnsUp && millis() - wifiStartMs > 15000) {
    Serial.println("[wifi] hotspot not found, falling back to AP");
    startAp();
  }
}

// ----------------------------------------------------------------- setup ---
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Sinag simulator " FW_VERSION " ===");

  prefs.begin("sinag-sim", false);
  S.mode   = prefs.getUChar("mode", MODE_INVERTER) == MODE_METER ? MODE_METER : MODE_INVERTER;
  S.impKwh = prefs.getDouble("imp", METER_START_KWH);
  S.expKwh = prefs.getDouble("exp", 0.0);
  knob     = modeKnobs()[0];

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetPinAttenuation(POT_PIN, ADC_11db);   // full 0–3.3 V range

  if (RS485_DE_PIN >= 0) { pinMode(RS485_DE_PIN, OUTPUT); digitalWrite(RS485_DE_PIN, LOW); }
  MB.begin(MB_BAUD, SERIAL_8N1, MB_RX_PIN, MB_TX_PIN);
  Serial.printf("[mode] %s -> Modbus slave id %d, %d 8N1, RX=%d TX=%d, %s\n", MODE_IDS[S.mode], activeSlaveId(),
                MB_BAUD, MB_RX_PIN, MB_TX_PIN, RS485_DE_PIN >= 0 ? "RS485 transceiver" : "direct UART (no RS485)");

  Wire.begin(LCD_SDA, LCD_SCL);
  uint8_t addr = findLcd();
  if (addr) {
    lcd = new LiquidCrystal_I2C(addr, LCD_COLS, LCD_ROWS);
    lcd->init();
    lcd->backlight();
    Serial.printf("[lcd] found at 0x%02X\n", addr);
  } else {
    Serial.println("[lcd] not found on I2C — check SDA/SCL/power (running without it)");
  }
  flash(isMeter() ? "Sinag Meter Sim" : "Sinag Inverter", "Simulator " FW_VERSION, 2000);

  for (int i = 0; i < 8; i++) readPot();
  lockKnob();                                   // don't let the knob jump the defaults at boot
  simTick(0);

  if (strcmp(WIFI_SSID, "YOUR_HOTSPOT_SSID") == 0 || strlen(WIFI_SSID) == 0) {
    startAp();
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    wifiStartMs = millis();
    Serial.printf("[wifi] connecting to \"%s\"...\n", WIFI_SSID);
  }

  web.on("/", HTTP_GET, [] { web.send_P(200, "text/html", INDEX_HTML); });
  web.on("/api/state", HTTP_GET, sendState);
  web.on("/api/control", HTTP_POST, handleControl);
  web.on("/api/control", HTTP_GET, handleControl);
  web.onNotFound([] { web.send(404, "text/plain", "not found"); });
  web.begin();
}

// ------------------------------------------------------------------ loop ---
void loop() {
  static uint32_t tPot = 0, tSim = 0, tLcd = 0, tLog = 0, tSave = 0;
  uint32_t now = millis();

  mbPoll();                                     // keep this first and frequent
  readButton();
  if (now - tPot >= 20)  { tPot = now; readPot(); }
  if (now - tSim >= 250) { tSim = now; simTick(0.25f); }
  mbPoll();
  if (now - tLcd >= 200) { tLcd = now; drawLcd(); }
  mbPoll();
  wifiLoop();
  web.handleClient();

  if (isMeter() && now - tSave >= 60000) { tSave = now; saveMeterReading(false); }

  if (now - tLog >= 5000) {
    tLog = now;
    if (isMeter())
      Serial.printf("[meter] %.1fV %.2fA %+.0fW PF%.2f %.2fHz | imp %.2f exp %.2f kWh %s%s| mb req %lu crcErr %lu\n",
                    S.mV, S.mI, S.mP, S.mPF, S.mHz, S.impKwh, S.expKwh, S.brownout ? "BROWNOUT " : "",
                    S.sag ? "LOW-V " : "", (unsigned long)mb.requests, (unsigned long)mb.crcErrors);
    else
      Serial.printf("[inverter] sun %d%% load %d%% | pv %d load %d grid %+d batt %+d soc %.1f%% temp %.1fC %s%s| mb req %lu crcErr %lu\n",
                    S.sunPct, S.loadPct, S.pvW, S.loadW, S.gridW, S.battW, S.soc, S.temp,
                    S.brownout ? "BROWNOUT " : "", S.fault ? "FAULT " : "",
                    (unsigned long)mb.requests, (unsigned long)mb.crcErrors);
  }
}
