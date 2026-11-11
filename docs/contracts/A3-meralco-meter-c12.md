# Contract A3 — Meralco-style utility meter, ANSI C12.18 / C12.19 (PROPOSED)

Meralco's AMI rollout uses **Aclara I-210+c** (residential) and **kV2c** meters
([Hubbell news, Mar 2026](https://www.hubbell.com/aclara/en/news/hubbell-partners-with-meralco-on-smart-meter-supply-contract)).
These ANSI-family meters expose local data through an **ANSI C12.18** port (optical
on the real meter) carrying **ANSI C12.19** tables, not Modbus. The simulator's
**MERALCO mode** speaks that protocol, so Sinag's connector is ready for a
utility-meter integration.

> A real Meralco meter belongs to Meralco and is sealed. Its port needs the utility's
> password, and connecting to it without Meralco's permission is not allowed. This mode
> is for demos, and for a future Meralco-sanctioned integration (optical probe or
> AMI head-end data).

## Link
- Same UART/RS485 bus as Modbus, 9600 8N1 (C12.18's default speed). No slave ID.
- Packet: `EE 00 <ctrl> 00 <len hi> <len lo> <data…> <crc lo> <crc hi>`.
  `ctrl` bit 5 toggles on each new request. CRC is CRC-16/X-25 over everything before it.
  **Verify the CRC variant against the C12.18 spec before connecting a real meter.**
- The receiver answers every packet with `06` (ACK) or `15` (NAK), then sends its response packet.

## Session (what the Sinag device does every 5 s)
| Step | Request data | Response |
|---|---|---|
| Identification | `20` | `00 00 02 00 00` |
| Logon | `50 <user id 2 B> <user 10 B>` | `00` |
| Security | `51 <password 20 B>` | `00` (the simulator accepts any password) |
| Read ST-23 | `30 00 17` | `00 <count 2 B> <table> <checksum>` |
| Read ST-28 | `30 00 1C` | same |
| Logoff, terminate | `52`, then `21` | `00` |

Partial reads (`3F <table 2 B> <offset 3 B> <count 2 B>`) are supported too.
Response codes: `00` ok · `01` error · `02` service not supported ·
`03` insufficient security clearance (read before logon) · `04` operation not possible (no such table) ·
`05` inappropriate action requested (offset past the end).
The table checksum is the two's complement of the table bytes' sum.

## Tables (fixed, simplified layouts; little-endian)
A real meter describes its layout in ST-0, ST-21, ST-12 and ST-15. The simulator
fixes these layouts instead:

| Table | Bytes | Fields |
|---|---|---|
| ST-0 general config | 10 | format control `02 00 00`, device class `"ACL "`, nameplate type `02` (electric) |
| ST-1 manufacturer ident | 32 | `"ACL "`, model `"I210+c  "`, HW 1.0, FW 2.0, serial `"SINAG-SIM-000001"` |
| ST-23 current register data | 13 | u8 demand resets · u32 Wh delivered (import) · u32 Wh received (export) · u32 Wh total |
| ST-28 present register data | 24 | i32 W (+ import / − export) · u32 V×100 · u32 A×1000 · i32 VA · i32 VAr · i16 PF×1000 · u16 Hz×100 |

## Output
The Sinag device publishes the same `sinag/{id}/meter` JSON as for the SDM120, with
`"meter_type": "ansi-c12"` (`"sdm120"` for the submeter). See Contract B.
