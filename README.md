# Rainwise-Wireless-Info

MK3-LR XBee data information

## Introduction

Recently I had a power failure at home.  When the power returned my Oracle display stopped displaying data.  Having had to replace the wireless module long ago on another Rainwise I assumed it was the receiver.  I have reached out to 4x different support contacts for Rainwise and gotten no response.  So what does a reasonable person do??? I have no idea but this is what I did.

### Goals

1. Decode the MK3-LR wireless data stream.
2. Build a replacement transmitter/receiver pair using LoRa, so the Oracle display keeps working without the original XBee link.
3. Keep logging the data locally. The vendor has switched logging systems and the IP-100 may no longer be able to send data to their server.

## Documentation

| File | Contents |
| --- | --- |
| README.md (this page) | Overview, packet framing and byte map, tools, test plan |
| [docs/protocol.md](docs/protocol.md) | How each field was confirmed (field notes), raw packet samples, IP-100 XML snapshot, sea-level pressure formula |
| [docs/hardware.md](docs/hardware.md) | XBee settings, the Oracle display's RJ11 link and how to identify it, LoRa replacement plan, wiring diagrams, US radio rules |
| [lora/README.md](lora/README.md) | RAK4631 firmware: radio settings, pins, building and flashing, bench test |

## Inspection

I started visually inspecting pieces.  The display lit up but showed no data.  The receiver box showed a blinking light.  Something was receiving data but not getting to the display (initial evaluation).

I disassembled the receiver box.  Found what I am assuming is mostly a voltage level shifter and a Digi XBee3 module.

- Ordered an XBee breakout board from Amazon
- Connected the module to the breakout board and plugged it into my PC
- Data was being received

The serial data stream is 9600 8N1.  This also matched some info I had located about the Digi XBee3 module.

## Data

### Packet framing

|     Item      |                                               Value                                                |
| ------------- | -------------------------------------------------------------------------------------------------- |
| Serial        | 9600 baud, 8N1                                                                                     |
| Packet length | 32 bytes                                                                                           |
| Header        | `33 80 00` (bytes 1-3)                                                                             |
| Byte order    | Big-endian (high byte first), except bytes 17-18 (see below)                                       |
| Checksum      | CRC-16/XMODEM over bytes 1-30, stored big-endian in bytes 31-32                                    |
| Packet rate   | About every 2 seconds                                                                              |
| Live fields   | Wind speed, wind direction and rain counter update in every packet                                 |
| Slow fields   | Temperature, humidity, pressure, voltages and bytes 15-18 update once per minute (around :29 past) |

**Checksum details (confirmed):** CRC-16/XMODEM, polynomial `0x1021`, initial value `0x0000`, no input/output reflection, no final XOR. Calculated over bytes 1-30 including the header. Verified against every distinct packet captured so far.

These parameters come from the [CRC RevEng catalogue][crc-cat], the standard reference for identifying CRCs. Every entry has a "check" value: the CRC of the ASCII text `123456789`. For CRC-16/XMODEM that's `0x31C3`, and `rainwise_logger.py --test` checks it. The catalogue's [legend page][crc-legend] explains what each parameter (width, poly, init, refin, refout, xorout) means. It's a good starting point for learning how CRCs work.

```python
def crc16_xmodem(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc

# valid when crc16_xmodem(pkt[0:30]) == (pkt[30] << 8) | pkt[31]
```

### Byte map

Status key: ✅ confirmed against the IP-100 · ⚠️ partly understood · ❓ unknown

| Byte  |                  Field                   |                        Format                         |  Example   |   Result    | Status |
| :---: | ---------------------------------------- | ----------------------------------------------------- | :--------: | ----------- | :----: |
| 01-03 | Header                                   | constant                                              | `33 80 00` |             |   ✅    |
| 04-05 | Outside temperature                      | signed 16-bit, °F × 10                                |  `01 2B`   | 29.9 °F     |   ✅    |
|  06   | Humidity                                 | 8-bit, %                                              |    `4A`    | 74 %        |   ✅    |
| 07-08 | Barometric pressure                      | 16-bit, inHg × 1000                                   |  `64 9B`   | 25.755 inHg |   ✅    |
| 09-10 | Wind speed                               | 16-bit, mph × 10, live                                |  `00 1E`   | 3.0 mph     |   ✅    |
| 11-12 | Wind direction                           | 16-bit, degrees, live                                 |  `00 2D`   | 45°         |   ✅    |
|  13   | Rain tip counter                         | 8-bit, 0.01 in per tip, wraps at 256                  |    `3D`    | 61          |   ✅    |
|  14   | Rain check byte                          | 255 − byte 13                                         |    `C2`    |             |   ✅    |
|  15   | Station voltage (as shown by the IP-100) | 8-bit, V ≈ 0.0291 × value + 0.20                      |    `EB`    | 7.04 V      |   ✅    |
|  16   | Status flags                             | bit 0 = low-voltage flag? (`04` normal, `05` flagged) |    `04`    |             |   ⚠️   |
| 17-18 | Unknown analog value                     | 16-bit, **low byte first**                            |  `B4 B4`   | 46260       |   ⚠️   |
|  19   | Unknown                                  | `00` so far                                           |    `00`    |             |   ❓    |
| 20-21 | Voltage-like reading A                   | 16-bit, V × 100 (assumed)                             |  `02 C4`   | 7.08        |   ❓    |
| 22-23 | Voltage-like reading B                   | 16-bit, V × 100 (assumed)                             |  `02 A6`   | 6.78        |   ❓    |
| 24-30 | Unused                                   | all `00` so far                                       |  `00 ...`  |             |   ❓    |
| 31-32 | Checksum                                 | CRC-16/XMODEM                                         |  `AC 5E`   |             |   ✅    |


How each field was confirmed, with raw captures and the IP-100 comparison, is in [docs/protocol.md](docs/protocol.md#field-notes). Why the pressure reads low and how it's corrected to sea level: [docs/protocol.md](docs/protocol.md#sea-level-pressure).

## Tools

### `rainwise_logger.py` (testing)

Reads the XBee breakout over USB, checks the CRC, decodes the known fields, polls the IP-100 XML at the same moment, and writes one CSV row per packet. Includes a `changed_bytes` column listing which byte positions differ from the previous packet.

```
pip install pyserial
python rainwise_logger.py --test                 # decode the built-in sample
python rainwise_logger.py --port COM5 --xml-url http://<ip100-address>/status.xml
python rainwise_logger.py --port COM5 --no-xml   # radio only
python rainwise_logger.py --port COM5 --elevation-ft <feet> --baro-offset <inHg>   # add sea-level pressure
```

### `rainwise_service.py` + `docker-compose.yml` (long-term logging)

Runs on the TrueNAS NAS as a custom Docker app ([TrueNAS – installing custom apps][truenas-apps]) and stores everything in SQLite (`rainwise.db`):

- `packets`: raw hex of every valid packet plus decoded fields, so history can be re-decoded later
- `ip100_xml`: IP-100 readings once a minute, plus the full XML

Inputs: XBee on USB serial, UDP port 5005 (for the future LoRa receiver, one 32-byte packet per datagram), and the IP-100 XML. Each can be turned off in the compose file.

Only one program can open the serial port at a time. While testing on the PC, run the container with `SERIAL_PORT: ""` so it still logs the XML.

### `lora/` (RAK4631 firmware)

RadioLib sketch for the XBee replacement, with a station role (UART in, LoRa out) and a receiver role (LoRa in, raw 32-byte packets out on USB and UART1). A PlatformIO project (`lora/platformio.ini`) with `station` and `receiver` environments, built and flashed from VS Code. Radio settings, pins, setup and the bench test are in [lora/README.md](lora/README.md).

## Test plan

|                                                                           Test                                                                            | Status |                                                   Result                                                    |
| --------------------------------------------------------------------------------------------------------------------------------------------------------- | :----: | ----------------------------------------------------------------------------------------------------------- |
| Connectivity (port, self-test, live data, XML)                                                                                                            |   ✅    | All passed 2026-10-25                                                                                       |
| Night baseline capture                                                                                                                                    |   ✅    | Confirmed CRC, 1-minute update of slow fields, ruled out solar on 22-23                                     |
| Rain: pour water in the gauge                                                                                                                             |   ✅    | Byte 13 = tip counter, 0.01 in/tip; byte 14 = 255 − byte 13                                                 |
| Wind: spin the anemometer, move the tail                                                                                                                  |   ✅    | 09-10 and 11-12 confirmed, live; gust is calculated downstream                                              |
| Daytime capture: compare 15, 17-18, 20-23 in sun vs. night                                                                                                |   ✅    | 11 h log 2026-10-26: byte 15 = IP-100 station volts; byte 16 bit 0 = low-voltage flag?; 20-23 still unknown |
| Sunlight model vs. byte 15 (panel true south, 60° tilt)                                                                                                   |   ✅    | Byte 15 follows temperature, not sunlight                                                                   |
| Multimeter: measure station battery and solar panel, note the minute, compare with bytes 15, 20-21, 22-23 (cold night and warm afternoon)                 |   ⬜    |                                                                                                             |
| Sea-level pressure: verify elevation with GPS, compare with nearest airport at :53 past the hour on several calm days, set offset                         |   ⬜    | First comparison ~0.07 inHg high                                                                            |
| Receiver: check whether anything is ever sent back to the station (XBee DIN pin)                                                                          |   ⬜    |                                                                                                             |
| XBee config: read both XBees in XCTU; confirm `AP` = 0 (transparent mode), note `BD` (baud), `D6`/`D7` (RTS/CTS flow control) and sleep settings          |   ⚠️   | Receiver done 2026-10-26: transparent, 9600 8N1, CTS on, 802.15.4. Station still to read                    |
| Station side: tap the station XBee's DIN pin (pin 3) with a 3.3 V USB-serial adapter and confirm it carries the same 32-byte packets the receiver outputs |   ⬜    |                                                                                                             |
| Receiver to display: identify the RJ11 link type (RS-485 is unconfirmed), capture it and compare with the XBee output (same bytes, or re-formatted?)       |   ⬜    |                                                                                                             |

## References

**Checksum**

- [CRC RevEng – 16-bit CRC catalogue][crc-cat]: parameters and check values for CRC-16/XMODEM and its relatives.
- [CRC RevEng – Catalogue legend][crc-legend]: what each CRC parameter means.

**Logging**

- [TrueNAS – Installing custom apps][truenas-apps]: the Install via YAML steps.

[crc-cat]: https://reveng.sourceforge.io/crc-catalogue/16.htm
[crc-legend]: https://reveng.sourceforge.io/crc-catalogue/legend.htm
[truenas-apps]: https://apps.truenas.com/managing-apps/installing-custom-apps/
