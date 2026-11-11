// =============================================================================
//  Sinag device (ESP32 #1) — the connector
//  Modbus RTU master on one bus, polling every 5 s:
//    inverter (slave 1, Contract A)  ->  sinag/{id}/telemetry  (Contract B)
//    smart meter (slave 2, SDM120)   ->  sinag/{id}/meter
//  Whichever device answers gets published. One that stops answering is retried
//  every 15 s, so the simulator can be in either mode without reflashing this.
//
//  Status LED
//    fast blink (5 Hz) ... no Wi-Fi
//    slow blink (1 Hz) ... Wi-Fi OK, no MQTT broker
//    double blink ........ MQTT OK, but no device is answering Modbus
//    solid on ............ all good (blinks off briefly on every publish)
// =============================================================================
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ModbusMaster.h>
#include <time.h>
#include "config.h"
#include "profiles.h"

#define FW_VERSION "0.2.0"

static const BrandProfile &P = PROFILE;
static const MeterProfile &MP = METER_PROFILE;

HardwareSerial &MB = Serial2;
ModbusMaster    invNode, meterNode;
WiFiClient      net;
PubSubClient    mqtt(net);

char deviceId[32];
char topicTelemetry[64];
char topicMeter[64];
char topicStatus[64];

float    values[F_COUNT];
float    meterValues[M_COUNT];
uint32_t publishFlashUntil = 0;

// Health of one Modbus device on the bus
struct Link {
  const char *name;
  bool        enabled;
  uint32_t    ok = 0, fail = 0, consecutive = 0, nextTry = 0;
  uint8_t     lastError = 0;
};
Link invLink   { "inverter", INVERTER_ENABLED != 0 };
Link meterLink { "meter",    METER_ENABLED != 0 };

// ---------------------------------------------------------------- RS485 ----
static void preTx()  { if (RS485_DE_PIN >= 0) digitalWrite(RS485_DE_PIN, HIGH); }
static void postTx() { if (RS485_DE_PIN >= 0) { MB.flush(); digitalWrite(RS485_DE_PIN, LOW); } }

static const char *mbErrorText(uint8_t e) {
  switch (e) {
    case ModbusMaster::ku8MBResponseTimedOut:   return "timeout (no reply: check TX/RX crossed, GND, power, slave id; RS485: swap A/B)";
    case ModbusMaster::ku8MBInvalidCRC:         return "bad CRC (noise or wrong baud)";
    case ModbusMaster::ku8MBInvalidSlaveID:     return "reply from wrong slave id";
    case ModbusMaster::ku8MBInvalidFunction:    return "reply has wrong function";
    case ModbusMaster::ku8MBIllegalFunction:    return "exception: illegal function";
    case ModbusMaster::ku8MBIllegalDataAddress: return "exception: illegal data address (check profile)";
    case ModbusMaster::ku8MBIllegalDataValue:   return "exception: illegal data value";
    case ModbusMaster::ku8MBSlaveDeviceFailure: return "exception: slave device failure";
    default:                                    return "unknown error";
  }
}

static uint8_t readBlock(ModbusMaster &node, uint8_t function, uint16_t start, uint16_t count) {
  return function == 4 ? node.readInputRegisters(start, count) : node.readHoldingRegisters(start, count);
}

// ------------------------------------------------------------- inverter ----
static float decode(const FieldMap &m, uint16_t raw) {
  int32_t v = m.isSigned ? (int32_t)(int16_t)raw : (int32_t)raw;
  return v * m.scale;
}

// One block read when the registers are close together, otherwise one read per field.
static uint8_t pollInverter() {
  int32_t lo = INT32_MAX, hi = -1;
  for (int i = 0; i < F_COUNT; i++) {
    if (P.f[i].reg < 0) continue;
    lo = min(lo, P.f[i].reg);
    hi = max(hi, P.f[i].reg);
  }
  if (hi < 0) return ModbusMaster::ku8MBIllegalDataAddress;

  float out[F_COUNT] = { 0 };
  if (hi - lo + 1 <= 60) {
    uint8_t r = readBlock(invNode, P.function, lo, hi - lo + 1);
    if (r != ModbusMaster::ku8MBSuccess) return r;
    for (int i = 0; i < F_COUNT; i++)
      if (P.f[i].reg >= 0) out[i] = decode(P.f[i], invNode.getResponseBuffer(P.f[i].reg - lo));
  } else {
    for (int i = 0; i < F_COUNT; i++) {
      if (P.f[i].reg < 0) continue;
      uint8_t r = readBlock(invNode, P.function, P.f[i].reg, 1);
      if (r != ModbusMaster::ku8MBSuccess) return r;
      out[i] = decode(P.f[i], invNode.getResponseBuffer(0));
    }
  }
  memcpy(values, out, sizeof values);
  return ModbusMaster::ku8MBSuccess;
}

// ---------------------------------------------------------------- meter ----
static float wordsToFloat(uint16_t a, uint16_t b, bool swap) {
  uint32_t u = swap ? ((uint32_t)b << 16) | a : ((uint32_t)a << 16) | b;
  float f;
  memcpy(&f, &u, 4);
  return isfinite(f) ? f : 0.0f;
}

// Reads the meter in as few requests as possible. ModbusMaster keeps at most 64
// registers per reply, so fields are grouped into windows of <= 64 registers
// (SDM120: 0x00..0x3F, then 0x46..0x4B -> 2 requests).
#define MB_MAX_WORDS 64
static uint8_t pollMeter() {
  float out[M_COUNT] = { 0 };
  bool  done[M_COUNT] = { false };
  for (int i = 0; i < M_COUNT; i++) {
    if (MP.reg[i] < 0 || done[i]) continue;
    int32_t start = MP.reg[i], end = MP.reg[i] + 1;          // inclusive last register
    for (int j = 0; j < M_COUNT; j++)
      if (!done[j] && MP.reg[j] >= start && MP.reg[j] + 1 < start + MB_MAX_WORDS) end = max(end, MP.reg[j] + 1);

    uint8_t r = readBlock(meterNode, MP.function, start, end - start + 1);
    if (r != ModbusMaster::ku8MBSuccess) return r;
    for (int j = 0; j < M_COUNT; j++) {
      if (done[j] || MP.reg[j] < start || MP.reg[j] + 1 > end) continue;
      uint16_t o = MP.reg[j] - start;
      out[j]  = wordsToFloat(meterNode.getResponseBuffer(o), meterNode.getResponseBuffer(o + 1), MP.wordSwap);
      done[j] = true;
    }
  }
  memcpy(meterValues, out, sizeof meterValues);
  return ModbusMaster::ku8MBSuccess;
}

// ----------------------------------------------------------------- MQTT ----
static uint32_t nowEpoch() {
  time_t t = time(nullptr);
  return t > 1700000000 ? (uint32_t)t : 0;      // 0 = no NTP time yet (backend uses receive time)
}

static void publishStatus() {
  JsonDocument doc;
  doc["online"] = true;
  doc["rssi"]   = WiFi.RSSI();
  doc["fw"]     = FW_VERSION;
  char buf[96];
  size_t n = serializeJson(doc, buf);
  mqtt.publish(topicStatus, (const uint8_t *)buf, n, true);
}

static void publishJson(const char *topic, JsonDocument &doc) {
  char buf[320];
  size_t n = serializeJson(doc, buf);
  bool ok = mqtt.connected() && mqtt.publish(topic, (const uint8_t *)buf, n, false);
  Serial.printf("[pub] %s %s %s\n", ok ? "OK " : "--", topic, buf);
  if (ok) publishFlashUntil = millis() + 80;
}

static void publishTelemetry() {
  JsonDocument doc;
  doc["ts"]         = nowEpoch();
  doc["pv_w"]       = lroundf(values[F_PV_W]);
  doc["load_w"]     = lroundf(values[F_LOAD_W]);
  doc["grid_w"]     = lroundf(values[F_GRID_W]);
  doc["batt_soc"]   = lroundf(values[F_BATT_SOC]);
  doc["batt_w"]     = lroundf(values[F_BATT_W]);
  doc["grid_on"]    = values[F_GRID_ON] > P.gridOnThreshold;
  doc["inv_temp_c"] = lroundf(values[F_INV_TEMP_C]);
  doc["fault_code"] = lroundf(values[F_FAULT_CODE]);
  publishJson(topicTelemetry, doc);
}

static void publishMeter() {
  JsonDocument doc;
  doc["ts"] = nowEpoch();
  for (int i = 0; i < M_COUNT; i++) {
    if (MP.reg[i] < 0) continue;
    uint8_t d = METER_FIELD_DECIMALS[i];
    if (d == 0) doc[METER_FIELD_NAMES[i]] = lroundf(meterValues[i]);
    else        doc[METER_FIELD_NAMES[i]] = serialized(String(meterValues[i], (unsigned int)d));   // avoid 230.1000061
  }
  doc["grid_on"] = meterValues[M_VOLTAGE] > 100.0f;
  publishJson(topicMeter, doc);
}

static bool mqttConnect() {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  const char *will = "{\"online\":false}";
  const char *user = strlen(MQTT_USER) ? MQTT_USER : nullptr;
  const char *pass = strlen(MQTT_PASS) ? MQTT_PASS : nullptr;
  Serial.printf("[mqtt] connecting to %s:%d as %s ... ", MQTT_HOST, MQTT_PORT, deviceId);
  if (mqtt.connect(deviceId, user, pass, topicStatus, 1, true, will)) {
    Serial.println("connected");
    publishStatus();
    return true;
  }
  Serial.printf("failed, state %d\n", mqtt.state());
  return false;
}

// ---------------------------------------------------------------- polling --
// Poll one device with back-off: after 3 misses in a row it's only retried every
// ABSENT_RETRY_MS (each miss costs a 2 s Modbus timeout).
static void service(Link &l, uint8_t (*poll)(), void (*publish)()) {
  if (!l.enabled) return;
  uint32_t now = millis();
  if (l.consecutive >= 3 && (int32_t)(now - l.nextTry) < 0) return;

  uint8_t r = poll();
  if (r == ModbusMaster::ku8MBSuccess) {
    if (l.consecutive) Serial.printf("[modbus] %s answering again (after %lu misses)\n", l.name, (unsigned long)l.consecutive);
    l.ok++;
    l.consecutive = 0;
    publish();
    return;
  }
  l.fail++;
  l.consecutive++;
  l.lastError = r;
  if (l.consecutive <= 3)
    Serial.printf("[modbus] %s read failed 0x%02X: %s\n", l.name, r, mbErrorText(r));
  if (l.consecutive >= 3) {
    l.nextTry = now + ABSENT_RETRY_MS;
    if (l.consecutive == 3) Serial.printf("[modbus] %s not found, retrying every %d s\n", l.name, ABSENT_RETRY_MS / 1000);
  }
}

static bool anyDeviceOk() {
  return (invLink.enabled && invLink.consecutive == 0 && invLink.ok) ||
         (meterLink.enabled && meterLink.consecutive == 0 && meterLink.ok);
}

// ------------------------------------------------------------------ LED ----
static void updateLed() {
  uint32_t t = millis();
  bool on;
  if (WiFi.status() != WL_CONNECTED)  on = (t / 100) % 2;            // fast blink
  else if (!mqtt.connected())         on = (t / 500) % 2;            // slow blink
  else if (!anyDeviceOk()) {                                          // double blink
    uint32_t p = t % 1200;
    on = p < 120 || (p > 240 && p < 360);
  } else                              on = t > publishFlashUntil;     // solid, flick off on publish
  digitalWrite(LED_PIN, on);
}

// ---------------------------------------------------------------- setup ----
void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(LED_PIN, OUTPUT);

  if (strlen(DEVICE_ID)) strlcpy(deviceId, DEVICE_ID, sizeof deviceId);
  else {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(deviceId, sizeof deviceId, "sinag-%02x%02x%02x", mac[3], mac[4], mac[5]);
  }
  snprintf(topicTelemetry, sizeof topicTelemetry, "sinag/%s/telemetry", deviceId);
  snprintf(topicMeter,     sizeof topicMeter,     "sinag/%s/meter", deviceId);
  snprintf(topicStatus,    sizeof topicStatus,    "sinag/%s/status", deviceId);

  Serial.printf("\n=== Sinag device %s  fw %s ===\n", deviceId, FW_VERSION);
  Serial.printf("[modbus] bus %lu 8N1, RX=%d TX=%d, %s\n", (unsigned long)P.baud, MB_RX_PIN, MB_TX_PIN,
                RS485_DE_PIN >= 0 ? "RS485 transceiver" : "direct UART (no RS485)");
  if (invLink.enabled)   Serial.printf("[modbus] inverter \"%s\" slave %d fn %d -> %s\n", P.name, P.slaveId, P.function, topicTelemetry);
  if (meterLink.enabled) Serial.printf("[modbus] meter    \"%s\" slave %d fn %d -> %s\n", MP.name, MP.slaveId, MP.function, topicMeter);
  if (invLink.enabled && meterLink.enabled) {
    if (MP.baud != P.baud) Serial.println("[modbus] WARNING: inverter and meter profiles use different baud rates (one bus = one baud)");
    if (MP.slaveId == P.slaveId) Serial.println("[modbus] WARNING: inverter and meter have the same slave id");
  }

  if (RS485_DE_PIN >= 0) { pinMode(RS485_DE_PIN, OUTPUT); digitalWrite(RS485_DE_PIN, LOW); }
  MB.begin(P.baud, SERIAL_8N1, MB_RX_PIN, MB_TX_PIN);
  ModbusMaster *nodes[] = { &invNode, &meterNode };
  invNode.begin(P.slaveId, MB);
  meterNode.begin(MP.slaveId, MB);
  for (ModbusMaster *n : nodes) {
    n->preTransmission(preTx);
    n->postTransmission(postTx);
    n->idle([] { mqtt.loop(); });               // keep MQTT alive while waiting on a slow device
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[wifi] connecting to \"%s\"...\n", WIFI_SSID);

  configTime(0, 0, NTP_SERVER, "pool.ntp.org");
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(15);
  mqtt.setSocketTimeout(3);
}

// ----------------------------------------------------------------- loop ----
void loop() {
  static uint32_t tPoll = 0, tStatus = 0, tMqttTry = 0, tWifiLost = 0;
  static bool     wifiWasUp = false, firstPoll = true;
  uint32_t now = millis();

  // Wi-Fi watchdog: auto-reconnect usually works; kick it if it's been down 20 s.
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiWasUp) Serial.printf("[wifi] connected, IP %s, RSSI %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    wifiWasUp = true;
    tWifiLost = now;
  } else {
    if (wifiWasUp) { Serial.println("[wifi] lost connection"); wifiWasUp = false; }
    if (now - tWifiLost > 20000) {
      Serial.println("[wifi] retrying...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      tWifiLost = now;
    }
  }

  // MQTT: reconnect every 3 s while Wi-Fi is up.
  if (WiFi.status() == WL_CONNECTED && !mqtt.connected() && now - tMqttTry > 3000) {
    tMqttTry = now;
    if (mqttConnect()) tStatus = now;
  }
  mqtt.loop();

  // Modbus poll + publish. Polls even without Wi-Fi so the serial log shows the link works.
  if (firstPoll || now - tPoll >= POLL_MS) {
    firstPoll = false;
    tPoll = now;
    service(invLink, pollInverter, publishTelemetry);
    service(meterLink, pollMeter, publishMeter);
  }

  if (mqtt.connected() && millis() - tStatus >= STATUS_MS) { tStatus = millis(); publishStatus(); }

  updateLed();
}
