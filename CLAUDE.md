# Rainwise MK-III-LR wireless project

Decoding and replacing the XBee radio link of a Rainwise MK-III-LR weather station (Oracle display, IP-100 network interface), and logging the data locally. README.md is the full write-up: byte map, test results, XBee settings, LoRa plan, wiring diagrams and references. Read it before changing anything.

Private, machine-specific details (IP addresses, elevation, location, radio network ID) are in `CLAUDE.local.md`, which is gitignored. Never copy them into README.md, code defaults, or anything committed.

## Working style (owner's standing preferences)

- **Always include references/sources** for explanations, formulas, specs and design choices (README "References" section uses reference-style Markdown links). The owner wants to learn from them.
- Verify facts from datasheets and official docs; mark anything unconfirmed as **verify** rather than stating it as fact (e.g. the RJ11 link type is NOT confirmed as RS-485).
- Keep the public repo clean: run a privacy scan (grep for the values in CLAUDE.local.md) before suggesting a commit.
- Prefer standard off-the-shelf boards; no custom PCBs.

## Layout

| Path | What |
| --- | --- |
| `README.md` | Main documentation (public) |
| `rainwise_logger.py` | PC test logger: XBee serial -> CSV, CRC check, decode, polls IP-100 XML. `--test` runs a self-test |
| `rainwise/rainwise_service.py` | Long-term logger for TrueNAS (Docker): serial + UDP 5005 + IP-100 XML -> SQLite, stores raw hex |
| `rainwise/docker-compose.yml` | TrueNAS custom app (public copy uses placeholders) |
| `solar/solar_model.py` | Clear-sky sun model (NOAA + PVEducation formulas); location via SOLAR_LAT/LON/TZ env vars |
| `diagrams/*.mmd`, `*.png` | Mermaid source + rendered wiring diagrams (also embedded in README) |

Quick checks: `python rainwise_logger.py --test` (CRC check value 0x31C3 and sample decode).

## Protocol summary (details in README)

- 9600 8N1, 32-byte packets, header `33 80 00`, CRC-16/XMODEM over bytes 1-30 stored big-endian in 31-32.
- Packet every ~2 s; wind speed/direction and rain are live, temp/humidity/pressure/voltages update once a minute.
- Confirmed: temp (4-5, signed, F x10), humidity (6), station pressure (7-8, inHg x1000), wind speed (9-10, mph x10), wind dir (11-12), rain tip counter (13, 0.01 in/tip, wraps at 256), check byte (14 = 255 - byte 13), byte 15 = IP-100 "station volts" (~0.0291 x value + 0.20).
- Partly known: byte 16 bit 0 = probable low-voltage flag; bytes 17-18 16-bit low-byte-first, unknown.
- Unknown: bytes 20-21 and 22-23 (noisy voltage-like readings). Bytes 19, 24-30 always zero.
- Byte 15 follows air temperature, not sunlight on the panel (panel faces true south at 60 deg tilt); likely temperature drift in the measurement. Night multimeter reading of the 6 V SLA battery will settle it.
- Sea-level pressure: NWS altimeter-setting formula, elevation and offset are settings.

## XBee findings

- Receiver XBee: XB3-24, function set Digi XBee3 802.15.4 TH, firmware 2006, AP=0 (transparent), 9600 8N1, D6=0, D7=1 (CTS on), SM=0, RO=3, DH/DL=0/0, MY=0, EE=0 (no encryption).
- Three receivers (Oracle box, IP-100, PC breakout) receive simultaneously, so extra listeners work.
- The original "busted" receiver box's XBee works; the fault is downstream (level shifting / RJ11 link / display).

## Direction

Replace the station transmitter and every receiver with RAKwireless RAK4631 (US915, "H"/standard, not -R) on RAK19003 mini base boards, firmware written with RadioLib so it can move to other SX1262 boards. Goals: distance, then cost/sourcing. US rules: use 500 kHz LoRa bandwidth (FCC 15.247 digital modulation) or frequency hopping. Power each board from 5 V (buck converter or USB), never from an XBee socket's 3.3 V (RAK19003 3.3 V pin is an output).

## Open tasks

1. Read the **station** XBee in XCTU (read only): function set, AP, D7, RO, MY, DL. Don't write settings or update firmware.
2. Night multimeter reading of the station battery vs byte 15 (and a warm-afternoon one).
3. Identify the Oracle black box -> display RJ11 link (power, RS-485, RS-232 or TTL) with a meter, then capture it. Table in README "Identifying the link with a meter".
4. Measure the IP-100 RJ11 port (nothing plugged in); only plug the black box in if pinouts match. Check whether the IP-100's internal radio is a socketed XBee.
5. Station-side DIN tap (pin 3) with a logic analyzer, optional if station AP=0.
6. Calibrate sea-level pressure offset against the nearest airport METAR at :53 on calm days; confirm elevation with GPS.
7. Write RadioLib firmware (transmit and receive roles) and bench-test with two RAK kits.
8. Update the NAS service for the RAK USB receiver (should work as a serial port already).
