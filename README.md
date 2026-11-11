# Sinag

Home solar and electricity monitor with a local Taglish AI assistant. It runs fully offline on one laptop.

```
simulator (ESP32 #2) → Modbus RTU → Sinag device (ESP32 #1) → MQTT → FastAPI + SQLite + Ollama → Flutter app
```

| Folder | What | Status |
|---|---|---|
| `firmware/inverter-sim/` | ESP32 simulator: hybrid inverter, SDM120 submeter, or Meralco-style ANSI C12.18 meter, LCD, knob, button, web dashboard | Done |
| `firmware/sinag-device/` | ESP32 connector: Modbus + ANSI C12.18 master → MQTT | Done |
| `docs/contracts/` | A: Modbus map · A2: submeter map · A3: Meralco C12.18 meter · B: MQTT · C: REST/WS API | C draft; A2, A3 and the B meter topic proposed |
| `server/` | FastAPI backend, AI service | To build |
| `app/` | Flutter app | To build |

Start with `firmware/README.md` for wiring and flashing, and `docs/contracts/` before writing any code.
Change a contract only after the project lead approves.
