#!/usr/bin/env python3
"""
Rainwise MK3-LR XBee stream logger / decoder.

Reads the 32-byte packets from the XBee (9600 8N1), verifies the CRC-16/XMODEM
checksum, decodes the known fields, grabs the IP-100 status XML at the same
moment, and writes everything to a CSV so unknown bytes can be matched up
against real values.

Requires:  pip install pyserial

Usage examples:
  python rainwise_logger.py --port COM5                 (Windows)
  python rainwise_logger.py --port /dev/ttyUSB0         (Linux)
  python rainwise_logger.py --port COM5 --no-xml        (skip the IP-100)
  python rainwise_logger.py --test                      (decode the sample packet)
"""

import argparse
import csv
import datetime as dt
import os
import sys
import urllib.request
import xml.etree.ElementTree as ET

HEADER = bytes([0x33, 0x80, 0x00])
PACKET_LEN = 32
SAMPLE = bytes.fromhex(
    "33 80 00 01 2B 4A 64 9B 00 1E 00 2D 3D C2 EB 04 "
    "B4 B4 00 02 C4 02 A6 00 00 00 00 00 00 00 AC 5E"
)


# ---------------------------------------------------------------- checksum
def crc16_xmodem(data: bytes) -> int:
    """CRC-16/XMODEM: poly 0x1021, init 0x0000, no reflection, no final XOR."""
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = (
                ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
            )
    return crc


def crc_ok(pkt: bytes) -> bool:
    return crc16_xmodem(pkt[:30]) == int.from_bytes(pkt[30:32], "big")


# ---------------------------------------------------------------- sea-level pressure
# Set from --elevation-ft / --baro-offset (or RAINWISE_ELEVATION_FT / RAINWISE_BARO_OFFSET).
ELEVATION_FT = 0.0
BARO_OFFSET = 0.0


def sea_level_inhg(
    station_inhg: float, elevation_ft: float, offset: float = 0.0
) -> float:
    """NWS altimeter setting: the 'sea level' barometer value airports and
    most weather networks report. Uses only station pressure + elevation,
    so it doesn't swing with temperature."""
    if station_inhg < 10:  # no/garbage reading
        return None
    if not elevation_ft:
        return round(station_inhg + offset, 2)
    n = 0.190284
    p_hpa = station_inhg * 33.8639
    h_m = elevation_ft * 0.3048
    a_hpa = ((p_hpa - 0.3) ** n + 8.4184960528e-5 * h_m) ** (1 / n)
    return round(a_hpa / 33.8639 + offset, 2)


# ---------------------------------------------------------------- decoding
def u16(pkt, i):
    """Big-endian unsigned 16-bit from 0-based index i."""
    return int.from_bytes(pkt[i : i + 2], "big")


def s16(pkt, i):
    """Big-endian signed 16-bit (temperature can go below zero)."""
    return int.from_bytes(pkt[i : i + 2], "big", signed=True)


def decode(pkt: bytes) -> dict:
    """Byte numbers in comments are 1-based, matching the README table."""
    return {
        "temp_f": s16(pkt, 3) / 10,  # bytes 4-5
        "humidity_pct": pkt[5],  # byte 6
        "baro_inhg": u16(pkt, 6) / 1000,  # bytes 7-8 (station pressure)
        "baro_sl_inhg": sea_level_inhg(u16(pkt, 6) / 1000, ELEVATION_FT, BARO_OFFSET),
        "wind_mph": u16(pkt, 8) / 10,  # bytes 9-10
        "wind_dir_deg": u16(pkt, 10),  # bytes 11-12
        "batt_v": u16(pkt, 19) / 100,  # bytes 20-21
        "supply2_v": u16(pkt, 21)
        / 100,  # bytes 22-23 (2nd voltage, tracks battery; NOT solar)
        # --- unknowns, logged several ways so patterns stand out ---
        "rain_count": pkt[12],  # byte 13: tip counter, 0.01 in/tip, wraps at 256
        "rain_chk_ok": pkt[12] + pkt[13] == 0xFF,  # byte 14 = 255 - byte 13
        "b15": pkt[14],  # D8/D9, flips with battery
        "b16": pkt[15],  # constant 04
        "b17_18_le": pkt[16]
        | (pkt[17] << 8),  # 16-bit, LOW byte first; inverse of battery
        "b19": pkt[18],
        "b24_30_hex": pkt[23:30].hex(" ").upper(),
    }


# ---------------------------------------------------------------- IP-100 XML
XML_FIELDS = {
    "xml_clock": "hardware/clock",
    "xml_station_v": "hardware/station_volts",
    "xml_temp_out": "weather/temperature_outside/current",
    "xml_humidity": "weather/humidity/current",
    "xml_pressure": "weather/pressure/current",
    "xml_precip": "weather/precipitation/current",
    "xml_wind_speed": "weather/wind/speed",
    "xml_wind_dir": "weather/wind/direction",
    "xml_gust_speed": "weather/wind/gust_speed",
    "xml_gust_dir": "weather/wind/gust_direction",
    "xml_temp_in": "weather/temperature_inside/current",
}


def fetch_xml(url: str, timeout: float) -> dict:
    out = {k: "" for k in XML_FIELDS}
    try:
        with urllib.request.urlopen(url, timeout=timeout) as r:
            root = ET.fromstring(r.read())
        for key, path in XML_FIELDS.items():
            el = root.find(path)
            if el is not None and el.text:
                out[key] = el.text.strip()
    except Exception as e:  # network hiccups shouldn't stop logging
        out["xml_clock"] = f"ERROR: {e}"
    return out


# ---------------------------------------------------------------- framing
class Framer:
    """Finds 32-byte packets that start with the header and pass the CRC.
    Re-syncs one byte at a time if a header match turns out to be bogus."""

    def __init__(self):
        self.buf = bytearray()
        self.bad = 0

    def feed(self, data: bytes):
        self.buf.extend(data)
        while True:
            i = self.buf.find(HEADER)
            if i < 0:
                del self.buf[:-2]  # keep a possible partial header
                return
            if i:
                del self.buf[:i]
            if len(self.buf) < PACKET_LEN:
                return
            pkt = bytes(self.buf[:PACKET_LEN])
            if crc_ok(pkt):
                del self.buf[:PACKET_LEN]
                yield pkt
            else:
                self.bad += 1
                del self.buf[:1]


# ---------------------------------------------------------------- main loop
def run(args):
    import serial  # pyserial

    ser = serial.Serial(
        args.port, args.baud, bytesize=8, parity="N", stopbits=1, timeout=1
    )
    framer = Framer()
    prev = None
    new_file = not os.path.exists(args.out)

    fields = (
        ["pc_time", "raw_hex", "changed_bytes"]
        + list(decode(SAMPLE).keys())
        + ([] if args.no_xml else list(XML_FIELDS.keys()))
    )

    with open(args.out, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        if new_file:
            w.writeheader()
        print(f"Logging {args.port} @ {args.baud} to {args.out}  (Ctrl+C to stop)")
        try:
            while True:
                for pkt in framer.feed(ser.read(64)):
                    now = dt.datetime.now().isoformat(timespec="seconds")
                    d = decode(pkt)
                    changed = ""
                    if prev:
                        changed = " ".join(
                            str(n + 1) for n in range(PACKET_LEN) if pkt[n] != prev[n]
                        )
                    prev = pkt
                    row = {
                        "pc_time": now,
                        "raw_hex": pkt.hex(" ").upper(),
                        "changed_bytes": changed,
                        **d,
                    }
                    if not args.no_xml:
                        row.update(fetch_xml(args.xml_url, args.xml_timeout))
                    w.writerow(row)
                    f.flush()
                    line = (
                        f"{now}  T={d['temp_f']:.1f}F  RH={d['humidity_pct']}%  "
                        f"P={d['baro_inhg']:.3f} (SL {d['baro_sl_inhg']})  W={d['wind_mph']:.1f}@{d['wind_dir_deg']}  "
                        f"B={d['batt_v']:.2f}V  V2={d['supply2_v']:.2f}V  "
                        f"rain#={d['rain_count']}  15={d['b15']}  17-18le={d['b17_18_le']}"
                    )
                    if not args.no_xml:
                        line += (
                            f"  | XML T={row['xml_temp_out']} W={row['xml_wind_speed']}"
                            f" G={row['xml_gust_speed']}@{row['xml_gust_dir']}"
                        )
                    if changed:
                        line += f"  | changed: {changed}"
                    print(line)
        except KeyboardInterrupt:
            print(f"\nStopped. Packets failing CRC/resync events: {framer.bad}")
        finally:
            ser.close()


def self_test():
    print("Sample packet:", SAMPLE.hex(" ").upper())
    print(
        f"CRC calc 0x{crc16_xmodem(SAMPLE[:30]):04X}  stored 0x{u16(SAMPLE, 30):04X}"
        f"  -> {'OK' if crc_ok(SAMPLE) else 'MISMATCH'}"
    )
    for k, v in decode(SAMPLE).items():
        print(f"  {k:14} {v}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--port", help="serial port, e.g. COM5 or /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--out", default="rainwise_log.csv")
    ap.add_argument(
        "--xml-url",
        default=os.environ.get("RAINWISE_XML_URL", "http://192.168.1.1/status.xml"),
        help="IP-100 status XML (or set RAINWISE_XML_URL)",
    )
    ap.add_argument("--xml-timeout", type=float, default=2.0)
    ap.add_argument(
        "--elevation-ft",
        type=float,
        default=float(os.environ.get("RAINWISE_ELEVATION_FT", "0")),
        help="barometer height above sea level, for sea-level pressure "
        "(or set RAINWISE_ELEVATION_FT); 0 = no correction",
    )
    ap.add_argument(
        "--baro-offset",
        type=float,
        default=float(os.environ.get("RAINWISE_BARO_OFFSET", "0")),
        help="inHg added after correction, to calibrate against a nearby airport",
    )
    ap.add_argument("--no-xml", action="store_true", help="don't poll the IP-100")
    ap.add_argument(
        "--test", action="store_true", help="decode the sample packet and exit"
    )
    a = ap.parse_args()
    ELEVATION_FT, BARO_OFFSET = a.elevation_ft, a.baro_offset
    if a.test:
        self_test()
    elif not a.port:
        ap.error("--port is required (or use --test)")
    else:
        run(a)
