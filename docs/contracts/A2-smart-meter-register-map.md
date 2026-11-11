# Contract A2 — Smart meter register map (PROPOSED — needs team-lead approval)

The simulator's **METER mode** emulates an Eastron **SDM120** single-phase kWh
meter. This is the most common Modbus energy meter, so a real one can replace the
simulator later with no firmware change.

| Setting   | Value |
|-----------|-------|
| Protocol  | Modbus RTU, same bus as the inverter |
| Slave ID  | **2** (the inverter is 1) |
| Serial    | 9600 baud, 8N1 (a real SDM120 ships at 2400: change it in the meter's menu) |
| Function  | 0x04 Read Input Registers (the simulator also answers 0x03 with the same map) |
| Encoding  | IEEE-754 float32, two registers per value, **high word first** |

| Reg (hex) | SDM "3xxxx" | Field            | Unit |
|-----------|-------------|------------------|------|
| 0x0000    | 30001       | Voltage          | V    |
| 0x0006    | 30007       | Current          | A    |
| 0x000C    | 30013       | Active power     | W (+ import, − export) |
| 0x0012    | 30019       | Apparent power   | VA   |
| 0x0018    | 30025       | Reactive power   | VAr  |
| 0x001E    | 30031       | Power factor     | −1…1 (sign follows power direction) |
| 0x0046    | 30071       | Frequency        | Hz   |
| 0x0048    | 30073       | Import energy    | kWh  |
| 0x004A    | 30075       | Export energy    | kWh  |
| 0x0156    | 30343       | Total energy     | kWh  |

Example: `0x4366 0x0000` = 230.0 V.

The Sinag device reads this in two requests: `0x0000 ×32` and `0x0046 ×6`.
