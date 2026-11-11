#pragma once
// ======================= Sinag device (connector) — config ===================
// ESP32 #1. Edit these, then upload.

// ---- Wi-Fi (team hotspot) and MQTT broker (Mosquitto on the server laptop)
#define WIFI_SSID     "YOUR_HOTSPOT_SSID"
#define WIFI_PASS     "YOUR_HOTSPOT_PASSWORD"
#define MQTT_HOST     "192.168.43.100"     // server laptop IP on the hotspot
#define MQTT_PORT     1883
#define MQTT_USER     ""                   // empty = no auth
#define MQTT_PASS     ""
#define DEVICE_ID     "sinag-001"          // empty "" = derive from MAC (sinag-a1b2c3)

// ---- Timing
#define POLL_MS       5000                 // Modbus poll + telemetry publish
#define STATUS_MS     30000                // retained status refresh
#define NTP_SERVER    "pool.ntp.org"       // or the laptop's IP if it runs an NTP server

// ---- Modbus link. UART2.
//      Today (no RS485 modules): wire ESP32-to-ESP32 directly, TX->RX crossed.
//      Later with MAX3485: set RS485_DE_PIN to the pin tied to DE+RE.
#define MB_RX_PIN     16
#define MB_TX_PIN     17
#define RS485_DE_PIN  -1                   // -1 = direct UART (no transceiver)

// ---- What's on the bus (see profiles.h). Both are polled each cycle; whichever
//      answers gets published. A device that stops answering is retried every
//      ABSENT_RETRY_MS so a missing one doesn't slow the other down.
#define INVERTER_ENABLED  1
#define PROFILE           PROFILE_SINAG_SIM     // inverter -> sinag/{id}/telemetry
#define METER_ENABLED     1
#define METER_PROFILE     METER_SDM120          // smart meter -> sinag/{id}/meter
#define ABSENT_RETRY_MS   15000

// ---- Status LED (on-board LED is GPIO 2 on most ESP32 DevKits)
#define LED_PIN       2
