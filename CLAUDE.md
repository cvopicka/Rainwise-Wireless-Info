# Rainwise MK-III-LR wireless project

Decoding and replacing the XBee radio link of a Rainwise MK-III-LR weather station (Oracle display, IP-100 network interface), and logging the data locally. README.md has the overview, byte map, tools and test plan; docs/protocol.md has the evidence for each field, raw samples and the sea-level pressure formula; docs/hardware.md has XBee settings, the display link, the LoRa plan and wiring diagrams. Read them before changing anything. Each doc keeps its own reference-style "References" section for what it cites.

Private, machine-specific details (IP addresses, elevation, location, radio network ID) are in `CLAUDE.local.md`, which is gitignored. Never copy them into README.md, code defaults, or anything committed.

## Working style (owner's standing preferences)

- **Always include references/sources** for explanations, formulas, specs and design choices (each doc's "References" section uses reference-style Markdown links). The owner wants to learn from them.
- Verify facts from datasheets and official docs; mark anything unconfirmed as **verify** rather than stating it as fact (e.g. the RJ11 link type is NOT confirmed as RS-485).
- Keep the public repo clean: run a privacy scan (grep for the values in CLAUDE.local.md) before suggesting a commit.
- Prefer standard off-the-shelf boards; no custom PCBs.

## Layout

| Path | What |
| --- | --- |
| `README.md` | Overview, byte map, tools, test plan (public) |
| `docs/protocol.md` | Field notes / evidence per byte, raw samples, XML snapshot, sea-level pressure |
| `docs/hardware.md` | XBee config, RJ11 display link, LoRa plan and options, wiring diagrams, FCC notes |
| `rainwise_logger.py` | PC test logger: XBee serial -> CSV, CRC check, decode, polls IP-100 XML. `--test` runs a self-test |
| `rainwise/rainwise_service.py` | Long-term logger for TrueNAS (Docker): serial + UDP 5005 + IP-100 XML -> SQLite, stores raw hex |
| `rainwise/docker-compose.yml` | TrueNAS custom app (public copy uses placeholders) |
| `solar/solar_model.py` | Clear-sky sun model (NOAA + PVEducation formulas); location via SOLAR_LAT/LON/TZ env vars |
| `diagrams/*.mmd`, `*.png` | Mermaid source + rendered wiring diagrams (also embedded in docs/hardware.md) |
| `lora/` | RAK4631 RadioLib firmware (`rainwise_lora/rainwise_lora.cpp`), PlatformIO project (`pio run -e station` / `-e receiver`), RAK board files in `lora/rakwireless/`, bench replay script; its own README. Remote-command key in gitignored `lora/rainwise_lora/local_config.h`. Every radio message starts with a 2-byte station ID (`STATION_ID` in local_config.h, like the XBee PAN ID); receiver USB output is tagged `RWP`+ID+packet, read into `station_id` by the logger and NAS service. Receiver settings in flash, per board: "rain fix" (`F`, keeps the Oracle's rain counter continuous across station restarts) and "listen to all stations" (`A`, for the NAS logger) |

Quick checks: `python rainwise_logger.py --test` (CRC check value 0x31C3 and sample decode).

## Protocol summary (details in README and docs/protocol.md)

- 9600 8N1, 32-byte packets, header `33 80 00`, CRC-16/XMODEM over bytes 1-30 stored big-endian in 31-32.
- Packet every ~2 s; wind speed/direction and rain are live, temp/humidity/pressure/voltages update once a minute.
- Confirmed: temp (4-5, signed, F x10), humidity (6), station pressure (7-8, inHg x1000), wind speed (9-10, mph x10), wind dir (11-12), rain tip counter (13, 0.01 in/tip, wraps at 256), check byte (14 = 255 - byte 13), byte 15 = IP-100 "station volts" (~0.0291 x value + 0.20).
- Partly known: byte 16 bit 0 = probable low-voltage flag; bytes 17-18 16-bit low-byte-first, unknown.
- Unknown: bytes 20-21 and 22-23 (noisy voltage-like readings). Bytes 19, 24-30 always zero.
- Byte 15 follows air temperature, not sunlight on the panel (panel faces true south at 60 deg tilt); likely temperature drift in the measurement. Night multimeter reading of the 6 V SLA battery will settle it.
- Sea-level pressure: NWS altimeter-setting formula, elevation and offset are settings.

## XBee findings

- Receiver XBee: XB3-24, function set Digi XBee3 802.15.4 TH, firmware 2006, AP=0 (transparent), 9600 8N1, D6=0, D7=1 (CTS on), SM=0, RO=3, DH/DL=0/0, MY=0, EE=0 (no encryption).
- Station XBee (read 2026-10-29): same model/function set/firmware, AP=0, **BD=7 (115200 8N1)**, D6=0, D7=1, **SM=1 (pin sleep via SLEEP_RQ pin 9)**, RO=3, **DH/DL=0/FFFF (broadcast)**, EE=0, ID/CH match. MY=0, D8=1 (SLEEP_RQ), D9=1 (ON/SLEEP output on; controller may read pin 13 instead of CTS, firmware doesn't drive it yet). Station firmware role reads UART1 at 115200; SLEEP_RQ in on J6 SDA (pin 13), CTS out on J6 SCL (pin 14); handshake mode (mirror/cts-low/released) settable over the air and saved in flash; status message every 60 s.
- Three receivers (Oracle box, IP-100, PC breakout) receive simultaneously, so extra listeners work.
- The original "busted" receiver box's XBee works; the fault is downstream (level shifting / RJ11 link / display).

## Direction

Replace the station transmitter and every receiver with RAKwireless RAK4631 (US915, "H"/standard, not -R) on RAK19003 mini base boards, firmware written with RadioLib so it can move to other SX1262 boards. Goals: distance, then cost/sourcing. US rules: use 500 kHz LoRa bandwidth (FCC 15.247 digital modulation) or frequency hopping. Power each board from 5 V (buck converter or USB), never from an XBee socket's 3.3 V (RAK19003 3.3 V pin is an output).

## Open tasks

1. Done 2026-10-29 and 2026-11-08 (station XBee settings above, all read).
2. Night multimeter reading of the station battery vs byte 15 (and a warm-afternoon one). First reading 2026-10-29 ~17:36: 6.92 V vs byte 15 ≈ 6.98-7.01 V. Daytime discharge test 2026-11-07: byte 15 matches the meter within its ±0.19 V tolerance (RadioShack 22-807, ±1.5 % +0.2 % FS); too coarse to test temperature drift; red LED on main board blinks every 2 s (matches packet rate, also in daylight 2026-11-08; likely activity light, verify); rain counter (byte 13) resets to 0 on power-up (docs/protocol.md).
3. Identify the Oracle black box -> display RJ11 link (power, RS-485, RS-232 or TTL) with a meter, then capture it. Table in docs/hardware.md "Identifying the link with a meter".
4. Measure the IP-100 RJ11 port (nothing plugged in); only plug the black box in if pinouts match. Check whether the IP-100's internal radio is a socketed XBee.
5. Which handshake lines the station controller uses: answered by the station firmware's status messages once installed (owner has no logic analyzer). Field visits are costly: do fiddly work indoors and prefer firmware/remote diagnostics over ladder trips.
6. Calibrate sea-level pressure offset against the nearest airport METAR at :53 on calm days; confirm elevation with GPS.
7. RadioLib firmware written and compiled (`lora/`); bench-test with the four RAK kits (bench steps in lora/README.md).
8. Update the NAS service for the RAK USB receiver (should work as a serial port already).
