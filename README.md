# Rainwise-Wireless-Info

MK3-LR XBee data information

## Introduction

Recently I had a power failure at home.  When the power returned my Oracle display stopped displaying data.  Having had to replace the wireless module long ago on another Rainwise I assumed it was the receiver.  I have reached out to 4x different support contacts for Rainwise and gotten no response.  So what does a reasonable person do??? I have no idea but this is what I did.

### Goals

1. Decode the MK3-LR wireless data stream.
2. Build a replacement transmitter/receiver pair using LoRa, so the Oracle display keeps working without the original XBee link.
3. Keep logging the data locally. The vendor has switched logging systems and the IP-100 may no longer be able to send data to their server.

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

| Byte  |         Field         |                Format                |  Example   |   Result    | Status |
| :---: | --------------------- | ------------------------------------ | :--------: | ----------- | :----: |
| 01-03 | Header                | constant                             | `33 80 00` |             |   ✅    |
| 04-05 | Outside temperature   | signed 16-bit, °F × 10               |  `01 2B`   | 29.9 °F     |   ✅    |
|  06   | Humidity              | 8-bit, %                             |    `4A`    | 74 %        |   ✅    |
| 07-08 | Barometric pressure   | 16-bit, inHg × 1000                  |  `64 9B`   | 25.755 inHg |   ✅    |
| 09-10 | Wind speed            | 16-bit, mph × 10, live               |  `00 1E`   | 3.0 mph     |   ✅    |
| 11-12 | Wind direction        | 16-bit, degrees, live                |  `00 2D`   | 45°         |   ✅    |
|  13   | Rain tip counter      | 8-bit, 0.01 in per tip, wraps at 256 |    `3D`    | 61          |   ✅    |
|  14   | Rain check byte       | 255 − byte 13                        |    `C2`    |             |   ✅    |
|  15   | Unknown analog value  | 8-bit                                |    `EB`    |             |   ❓    |
|  16   | Unknown               | constant `04` so far                 |    `04`    |             |   ❓    |
| 17-18 | Unknown analog value  | 16-bit, **low byte first**           |  `B4 B4`   | 46260       |   ⚠️   |
|  19   | Unknown               | `00` so far                          |    `00`    |             |   ❓    |
| 20-21 | Battery voltage       | 16-bit, V × 100                      |  `02 C4`   | 7.08 V      |   ✅    |
| 22-23 | Second supply voltage | 16-bit, V × 100                      |  `02 A6`   | 6.78 V      |   ⚠️   |
| 24-30 | Unused                | all `00` so far                      |  `00 ...`  |             |   ❓    |
| 31-32 | Checksum              | CRC-16/XMODEM                        |  `AC 5E`   |             |   ✅    |

### Field notes

- **Temperature, humidity, pressure, wind direction:** match the IP-100 XML on every minute of the 2026-10-25 capture (e.g. 29.4 °F / 75 % / 26.272 vs 26.27 inHg / 337°).
- **Wind speed (09-10):** confirmed 2026-10-25 by spinning the anemometer by hand. Updates in every packet and matched the IP-100 value exactly at each step (1.2 → 2.8 → 0.0 mph). The 2024 mismatch (3.0 vs 5.4) was just a timing difference between packet and snapshot.
- **Wind gust:** not in the packet. The IP-100 tracked a 2.8 mph gust that equals the highest live speed sent, so gust is calculated by the receiving end from the live wind speed.
- **Wind direction (11-12):** confirmed 2026-10-25 by moving the tail: 337° → 135° → 157°, matching the IP-100 and updating live.
- **Rain (13-14):** confirmed 2026-10-25. About 4 oz of water poured into the gauge: byte 13 went `E1` → `E9` (8 tips) and the IP-100 daily rain went 0.00 → 0.08 in. The Oracle YTD went 10.95 → 11.03 in (+0.08). So **0.01 in per tip**. Byte 14 always equals 255 − byte 13 (`E1`/`1E`, `E9`/`16`, `3D`/`C2`), a simple check byte. Tips show up in the next packet, within about 2 s.
- **Rain counter wrap:** byte 13 is a running 8-bit count, not a daily or YTD total, and rolls over from 255 to 0. Rain amount = (new − old) mod 256 × 0.01 in. The station sends a packet every 2 s, so a wrap can't be missed in practice.
- **Byte 15:** flips between `D8` and `D9` in step with the battery voltage (`D8` when battery is higher). Probably a raw analog reading, not a counter.
- **Bytes 17-18:** read low byte first (`byte18 × 256 + byte17`) they form a smooth value around 45,500-46,000 that moves opposite to the battery voltage. Read high byte first, byte 17 looks like noise. Probably a raw ADC channel, possibly a reference voltage. Not wind gust: it only changes at the once-a-minute update and didn't react to the anemometer test.
- **Bytes 22-23:** originally guessed as solar panel voltage. **Ruled out:** at night it still reads 6.74-7.07 V, always about 0.2-0.3 V below the battery and moving with it. Most likely a supply rail after a diode or regulator.
- **Bytes 24-30:** always zero. Probably slots for sensors this station doesn't have (solar radiation, UV, extra probes).
- **Pressure is station pressure.** The value in bytes 7-8 is the actual air pressure at the station, not corrected to sea level. At higher elevations it reads well below the ~30 inHg people expect. See [Sea-level pressure](#sea-level-pressure).
- **IP-100 `station_volts`:** reported 6.5 V during the night capture, which doesn't directly match bytes 20-21 or 22-23. Don't use it as a reference until understood.
- **Header:** may contain a station ID or type. Can't test against a second station (the other Rainwise transmitter is not XBee).

### RAW samples

2024-03-23 (original sample, matches XML snapshot below):

```
33 80 00 01 2B 4A 64 9B 00 1E 00 2D 3D C2 EB 04 B4 B4 00 02 C4 02 A6 00 00 00 00 00 00 00 AC 5E
```

2026-10-25 night capture (calm, dry, dark), one packet per minute:

```
21:09:57  33 80 00 01 28 4B 66 9D 00 00 01 51 E1 1E D9 04 BC B3 00 02 C3 02 A2 00 00 00 00 00 00 00 66 18
21:10:29  33 80 00 01 28 4B 66 9D 00 00 01 51 E1 1E D8 04 80 B2 00 02 CF 02 B8 00 00 00 00 00 00 00 8D 1F
21:11:29  33 80 00 01 27 4B 66 A3 00 00 01 51 E1 1E D8 04 EE B1 00 02 D1 02 BB 00 00 00 00 00 00 00 31 82
21:12:29  33 80 00 01 26 4B 66 9D 00 00 01 51 E1 1E D9 04 B4 B3 00 02 C6 02 A8 00 00 00 00 00 00 00 34 F7
21:13:35  33 80 00 01 26 4B 66 A0 00 00 01 51 E1 1E D9 04 C2 B3 00 02 C3 02 A2 00 00 00 00 00 00 00 B5 BC
21:14:29  33 80 00 01 25 4B 66 9D 00 00 01 51 E1 1E D8 04 D6 B2 00 02 D3 02 C3 00 00 00 00 00 00 00 70 71
```

IP-100 at 21:14:22: 29.3 °F, 75 %, 26.27 inHg, wind 0.0 @ 337°, gust 0.0, rain 0.00, inside 64.0 °F, station_volts 6.5.

2026-10-25 hands-on test (anemometer spun, tail moved, ~4 oz water poured in):

```
                                         spd   dir   rain
21:28:29  33 80 00 01 1D 4B 66 9D 00 1C 01 51 E1 1E ...   2.8 mph  337°  IP-100 gust 2.8
21:29:45  33 80 00 01 1C 4B 66 A0 00 00 00 87 E1 1E ...   0.0      135°
21:29:50  33 80 00 01 1C 4B 66 A0 00 00 00 9D E1 1E ...   0.0      157°
21:30:05  ... 00 00 00 9D E3 1C ...                                       IP-100 rain 0.02
21:30:07  ... 00 00 00 9D E5 1A ...                                       0.04
21:30:09  ... 00 00 00 9D E7 18 ...                                       0.06
21:30:11  ... 00 00 00 9D E8 17 ...                                       0.07
21:30:13  33 80 00 01 1C 4B 66 A0 00 00 00 9D E9 16 D8 04 99 B2 00 02 CC 02 B1 00 00 00 00 00 00 00 6A FB   0.08
```

### Closest XML Snapshot (2024-03-23)

```XML
<?xml version="1.0" encoding="ISO-8859-1"?>
<!-- Weather and hardware paramters for the IP-100, min's, max's and accumulations are for the current day -->
<status>
 <hardware>
   <serial_number></serial_number>
   <firmware_version>2083</firmware_version>
   <clock>2024/03/23 12:06:48</clock>
   <station_volts>7.0</station_volts>
   <network>
     <MAC_address></MAC_address>
     <IP_address></IP_address>
  <subnet></subnet>
  <gateway></gateway>
   </network>
   <interval>1</interval>
   <base_units>English</base_units>
   <speed_units>mph</speed_units>
 </hardware>

 <weather>
 <temperature_outside>
  <current>29.9</current>
  <max>30.0</max>
  <min>26.3</min>
 </temperature_outside>

 <humidity>
  <current>74</current>
  <max>74</max>
  <min>67</min>
 </humidity>

 <pressure>
  <current>25.76</current>
  <max>25.95</max>
  <min>25.76</min>
 </pressure>

 <precipitation>
  <current>0.00</current>
 </precipitation>

 <wind>
  <speed>5.4</speed>
  <direction>45</direction>
  <gust_speed>7.2</gust_speed>
  <gust_direction>45</gust_direction>
 </wind>

 <temperature_inside>
  <current>60.0</current>
  <max>62.0</max>
  <min>60.0</min>
 </temperature_inside>
 </weather>
</status>
```

The IP-100 serves its status XML live at `http://<ip100-address>/status.xml` (replace with your IP-100's address on your network).

## Sea-level pressure

The station sends **station pressure**: the real air pressure where the barometer sits. Weather reports, airports and most weather websites show pressure **corrected to sea level**, so stations at different elevations can be compared. At a few thousand feet the difference is about 4 inHg, so an uncorrected reading looks "wrong" next to the local forecast even though it isn't.

Both tools add a `baro_sl_inhg` value alongside the raw `baro_inhg`. It uses the NWS **altimeter setting** formula, the same correction airports report in METARs:

```
P  = station pressure in hPa (inHg × 33.8639)
h  = barometer elevation in meters (feet × 0.3048)
A  = ((P − 0.3)^0.190284 + 8.4184960528e-5 × h)^(1 / 0.190284)
sea-level inHg = A / 33.8639 + offset
```

It needs only pressure and elevation, so unlike the full mean-sea-level reduction it doesn't swing with outside temperature.

**Setup:**

1. Find the elevation of the barometer itself, in feet (the Oracle display or IP-100 box, wherever the pressure sensor is, not the ground at the mast). A GPS, a topo map or an online elevation lookup works.
2. Logger: `--elevation-ft <feet>` or set `RAINWISE_ELEVATION_FT`. Service: `ELEVATION_FT` in `docker-compose.yml`.
3. Calibrate: compare `baro_sl_inhg` with the altimeter setting from the nearest airport METAR on a calm day with steady pressure. Put the difference in `--baro-offset` / `BARO_OFFSET`. Small differences (±0.03 inHg) are normal sensor tolerance.

The elevation is kept out of the repo on purpose: it's a setting, not a value in the code. With elevation `0` no correction is applied.

Raw station pressure is always stored too, so the correction can be redone later if the elevation or offset changes.

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

Runs on the TrueNAS NAS as a custom Docker app and stores everything in SQLite (`rainwise.db`):

- `packets`: raw hex of every valid packet plus decoded fields, so history can be re-decoded later
- `ip100_xml`: IP-100 readings once a minute, plus the full XML

Inputs: XBee on USB serial, UDP port 5005 (for the future LoRa receiver, one 32-byte packet per datagram), and the IP-100 XML. Each can be turned off in the compose file.

Only one program can open the serial port at a time. While testing on the PC, run the container with `SERIAL_PORT: ""` so it still logs the XML.

## Test plan

|                                                               Test                                                                | Status |                                 Result                                  |
| --------------------------------------------------------------------------------------------------------------------------------- | :----: | ----------------------------------------------------------------------- |
| Connectivity (port, self-test, live data, XML)                                                                                    |   ✅    | All passed 2026-10-25                                                   |
| Night baseline capture                                                                                                            |   ✅    | Confirmed CRC, 1-minute update of slow fields, ruled out solar on 22-23 |
| Rain: pour water in the gauge                                                                                                     |   ✅    | Byte 13 = tip counter, 0.01 in/tip; byte 14 = 255 − byte 13             |
| Wind: spin the anemometer, move the tail                                                                                          |   ✅    | 09-10 and 11-12 confirmed, live; gust is calculated downstream          |
| Daytime capture: compare 15, 17-18, 20-23 in sun vs. night                                                                        |   ⬜    |                                                                         |
| Sea-level pressure: verify elevation with GPS, compare with nearest airport at :53 past the hour on several calm days, set offset |   ⬜    | First comparison ~0.07 inHg high                                        |
| Receiver: check whether anything is ever sent back to the station (XBee DIN pin)                                                  |   ⬜    |                                                                         |

## Receiver to Display Pinout

- 4 pins
- 9 V power supply
- Suspect RS-485 or RS-422, half duplex

To identify: with a meter, look for a pair that sits at a steady few hundred mV while idle and flickers during transmissions (A/B data pair). The other two pins are likely 9 V and ground. Then try a USB-RS485 adapter on A/B at 9600 8N1.

## LoRa replacement plan

- **Transmitter:** read the 32-byte packets from the station's controller at 9600 8N1 and send them over LoRa. Only the framing (header, length, CRC) is needed, not the full decode.
- **Rate:** wind speed, direction and rain change in every 2-second packet, and the display calculates gusts from those live values. So the link should forward every packet, or at least every packet that changed. At about 60 ms of airtime per packet that's around 3 % of the channel, well within LoRa limits. Sending once a minute would lose gusts.
- **Receiver (ESP32 + LoRa):** check the CRC, write the original packet to the Oracle display port so it sees the same stream as before, and forward the packet over Wi-Fi (UDP 5005) to the NAS logger.
- **Hardware notes:** the XBee runs on 3.3 V, so a 3.3 V LoRa board can share its power and UART pins. Check whether the boards use the XBee's sleep or reset lines.
