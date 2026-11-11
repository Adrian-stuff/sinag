# Sinag

Home solar and electricity monitor with a local Taglish AI assistant. It runs fully offline on one laptop.

```
simulator (ESP32 #2) → Modbus RTU → Sinag device (ESP32 #1) → MQTT → FastAPI + SQLite + Ollama → Flutter app
```

| Folder | What | Status |
|---|---|---|
| `firmware/inverter-sim/` | ESP32 simulator: hybrid inverter or SDM120 smart meter, LCD, knob, button, web dashboard | Done |
| `firmware/sinag-device/` | ESP32 connector: Modbus master → MQTT | Done |
| `docs/contracts/` | A: Modbus map · A2: smart meter map · B: MQTT · C: REST/WS API | C draft, A2/B meter topic proposed |
| `server/` | FastAPI backend, AI service | To build |
| `app/` | Flutter app | To build |

Start with `firmware/README.md` for wiring and flashing, and `docs/contracts/` before writing any code.
Change a contract only after the project lead (Adrian) approves.
