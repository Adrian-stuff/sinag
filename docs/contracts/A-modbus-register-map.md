# Contract A — Modbus register map (Inverter simulator ↔ Sinag device)

| Setting      | Value                                   |
|--------------|-----------------------------------------|
| Protocol     | Modbus RTU                              |
| Slave ID     | 1                                       |
| Serial       | 9600 baud, 8N1                          |
| Function     | 0x03 Read Holding Registers (0x04 also answered with the same map) |
| Block        | Start 0, count 8                        |

| Reg | Field        | Unit / meaning                                         | Encoding |
|-----|--------------|--------------------------------------------------------|----------|
| 0   | `pv_w`       | Solar power, W                                         | uint16   |
| 1   | `load_w`     | Home use, W                                            | uint16   |
| 2   | `grid_w`     | Grid power, W (+ import, − export)                     | **int16** (two's complement) |
| 3   | `batt_soc`   | Battery charge, %                                      | uint16 (0–100) |
| 4   | `batt_w`     | Battery power, W (+ discharging, − charging)           | **int16** (two's complement) |
| 5   | `grid_on`    | 1 = grid available, 0 = brownout                       | uint16   |
| 6   | `inv_temp_c` | Inverter temperature, °C                               | int16    |
| 7   | `fault_code` | 0 = OK, otherwise fault code (21 = overheating)        | uint16   |

Signed fields: a raw value of `64136` means `-1400` (`raw - 65536` when `raw > 32767`).

A real inverter uses its own register numbers. The Sinag device firmware has a
"brand profile" table (`firmware/sinag-device/profiles.h`) that maps a brand's
registers, scale and sign to these same fields.
