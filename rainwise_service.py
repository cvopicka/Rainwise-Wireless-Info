#!/usr/bin/env python3
"""
Rainwise MK3-LR logging service.

Inputs (each optional, enable via environment variables):
  SERIAL_PORT   XBee breakout on USB, e.g. /dev/ttyUSB0   (empty = off)
  UDP_PORT      packets forwarded by a future ESP32/LoRa receiver,
                one 32-byte packet per datagram (raw bytes or hex text)  (0 = off)
  XML_URL       IP-100 status XML, polled every XML_INTERVAL seconds  (empty = off)

Storage: SQLite at DB_PATH. Every packet keeps its raw hex, so history can be
re-decoded later once the unknown bytes are figured out.
"""

import os
import queue
import socket
import sqlite3
import threading
import time
import urllib.request
import xml.etree.ElementTree as ET
from datetime import datetime, timezone

# ------------------------------------------------------------------ config
SERIAL_PORT = os.environ.get("SERIAL_PORT", "").strip()
BAUD = int(os.environ.get("BAUD", "9600"))
UDP_PORT = int(os.environ.get("UDP_PORT", "5005"))
XML_URL = os.environ.get("XML_URL", "").strip()
XML_INTERVAL = float(os.environ.get("XML_INTERVAL", "60"))
DB_PATH = os.environ.get("DB_PATH", "/data/rainwise.db")
STATS_EVERY = float(os.environ.get("STATS_EVERY", "600"))
ELEVATION_FT = float(
    os.environ.get("ELEVATION_FT", "0") or 0
)  # 0 = no sea-level correction
BARO_OFFSET = float(
    os.environ.get("BARO_OFFSET", "0") or 0
)  # inHg, calibration vs. airport

HEADER = bytes([0x33, 0x80, 0x00])
PACKET_LEN = 32


def log(msg):
    print(f"{datetime.now().isoformat(timespec='seconds')}  {msg}", flush=True)


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


# ------------------------------------------------------------------ protocol
def crc16_xmodem(data: bytes) -> int:
    """https://reveng.sourceforge.io/crc-catalogue/16.htm (check "123456789" = 0x31C3)"""
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = (
                ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
            )
    return crc


def crc_ok(pkt: bytes) -> bool:
    return len(pkt) == PACKET_LEN and crc16_xmodem(pkt[:30]) == int.from_bytes(
        pkt[30:32], "big"
    )


def u16(p, i):
    return int.from_bytes(p[i : i + 2], "big")


def sea_level_inhg(station_inhg: float) -> float:
    """NWS altimeter setting (what airports and most weather networks report).
    Formula: https://www.weather.gov/media/epz/wxcalc/altimeterSetting.pdf"""
    if station_inhg < 10:  # no/garbage reading
        return None
    if not ELEVATION_FT:
        return round(station_inhg + BARO_OFFSET, 2)
    n = 0.190284
    k = 1013.25**n * 0.0065 / 288  # = 8.42288e-5, NWS constants
    p_hpa = station_inhg * 33.8639
    a_hpa = ((p_hpa - 0.3) ** n + k * ELEVATION_FT * 0.3048) ** (1 / n)
    return round(a_hpa / 33.8639 + BARO_OFFSET, 2)


def decode(p: bytes) -> dict:
    """Byte numbers in comments are 1-based, matching the README table."""
    return {
        "temp_f": int.from_bytes(p[3:5], "big", signed=True) / 10,  # 4-5
        "humidity_pct": p[5],  # 6
        "baro_inhg": u16(p, 6) / 1000,  # 7-8 station pressure
        "baro_sl_inhg": sea_level_inhg(u16(p, 6) / 1000),  # corrected to sea level
        "wind_mph": u16(p, 8) / 10,  # 9-10
        "wind_dir_deg": u16(p, 10),  # 11-12
        "rain_count": p[12],  # 13 tip counter, 0.01 in/tip, wraps at 256
        "rain_chk_ok": int(p[12] + p[13] == 0xFF),  # 14 = 255 - byte 13
        "station_v": round(0.0291 * p[14] + 0.20, 2),  # 15 = IP-100 station_volts
        "low_v_flag": p[15] & 0x01,  # 16 bit 0, low-voltage flag?
        "b15": p[14],  # 15 raw
        "b16": p[15],  # 16 raw (04 / 05)
        "b17_18_le": p[16] | (p[17] << 8),  # 17-18 16-bit LOW byte first
        "b19": p[18],  # 19
        "v20_21": u16(p, 19) / 100,  # 20-21 voltage-like, noisy
        "v22_23": u16(p, 21) / 100,  # 22-23 voltage-like, noisy
        "b24_30_hex": p[23:30].hex(" ").upper(),  # 24-30 unused sensors?
    }


DECODED_COLS = list(decode(bytes([0x33, 0x80, 0x00] + [0] * 29)).keys())


class Framer:
    """Pulls CRC-valid 32-byte packets out of a byte stream, re-syncing as needed."""

    def __init__(self):
        self.buf = bytearray()
        self.bad = 0

    def feed(self, data: bytes):
        self.buf.extend(data)
        while True:
            i = self.buf.find(HEADER)
            if i < 0:
                del self.buf[:-2]
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


# ------------------------------------------------------------------ database
SCHEMA = f"""
CREATE TABLE IF NOT EXISTS packets (
    id INTEGER PRIMARY KEY,
    ts_utc TEXT NOT NULL,
    source TEXT NOT NULL,
    raw_hex TEXT NOT NULL,
    {", ".join(c + (" TEXT" if c.endswith("_hex") else " REAL") for c in DECODED_COLS)}
);
CREATE INDEX IF NOT EXISTS packets_ts ON packets(ts_utc);

CREATE TABLE IF NOT EXISTS ip100_xml (
    id INTEGER PRIMARY KEY,
    ts_utc TEXT NOT NULL,
    clock TEXT, station_v REAL, temp_out REAL, humidity REAL, pressure REAL,
    precip REAL, wind_speed REAL, wind_dir REAL, gust_speed REAL, gust_dir REAL,
    temp_in REAL, raw_xml TEXT
);
CREATE INDEX IF NOT EXISTS ip100_ts ON ip100_xml(ts_utc);
"""

XML_PATHS = {
    "clock": "hardware/clock",
    "station_v": "hardware/station_volts",
    "temp_out": "weather/temperature_outside/current",
    "humidity": "weather/humidity/current",
    "pressure": "weather/pressure/current",
    "precip": "weather/precipitation/current",
    "wind_speed": "weather/wind/speed",
    "wind_dir": "weather/wind/direction",
    "gust_speed": "weather/wind/gust_speed",
    "gust_dir": "weather/wind/gust_direction",
    "temp_in": "weather/temperature_inside/current",
}


def db_writer(q: queue.Queue):
    """Single thread owns the SQLite connection; everyone else enqueues rows."""
    os.makedirs(os.path.dirname(DB_PATH) or ".", exist_ok=True)
    con = sqlite3.connect(DB_PATH)
    con.execute("PRAGMA journal_mode=WAL")
    con.executescript(SCHEMA)
    con.commit()
    log(f"database ready at {DB_PATH}")
    while True:
        table, row = q.get()
        cols = ", ".join(row)
        marks = ", ".join("?" for _ in row)
        try:
            con.execute(
                f"INSERT INTO {table} ({cols}) VALUES ({marks})", list(row.values())
            )
            con.commit()
        except sqlite3.Error as e:
            log(f"db error: {e}")


# ------------------------------------------------------------------ inputs
stats = {"serial": 0, "udp": 0, "udp_bad": 0, "xml": 0, "xml_err": 0}


def store_packet(q, pkt, source):
    q.put(
        (
            "packets",
            {
                "ts_utc": utc_now(),
                "source": source,
                "raw_hex": pkt.hex(" ").upper(),
                **decode(pkt),
            },
        )
    )


def serial_reader(q):
    import serial  # pyserial

    framer = Framer()
    while True:
        try:
            with serial.Serial(
                SERIAL_PORT, BAUD, bytesize=8, parity="N", stopbits=1, timeout=1
            ) as ser:
                log(f"serial open: {SERIAL_PORT} @ {BAUD}")
                while True:
                    for pkt in framer.feed(ser.read(64)):
                        stats["serial"] += 1
                        store_packet(q, pkt, "serial")
        except Exception as e:
            log(f"serial error ({e}); retrying in 10 s")
            time.sleep(10)


def udp_listener(q):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", UDP_PORT))
    log(f"udp listening on {UDP_PORT}")
    while True:
        data, addr = sock.recvfrom(1024)
        pkt = data
        if len(data) != PACKET_LEN:  # allow hex text like "33 80 00 ..."
            try:
                pkt = bytes.fromhex(data.decode("ascii", "ignore"))
            except ValueError:
                pkt = b""
        if crc_ok(pkt) and pkt[:3] == HEADER:
            stats["udp"] += 1
            store_packet(q, pkt, f"udp:{addr[0]}")
        else:
            stats["udp_bad"] += 1


def num(s):
    try:
        return float(s)
    except (TypeError, ValueError):
        return None


def xml_poller(q):
    log(f"polling IP-100 at {XML_URL} every {XML_INTERVAL:g} s")
    while True:
        try:
            with urllib.request.urlopen(XML_URL, timeout=5) as r:
                raw = r.read()
            root = ET.fromstring(raw)
            row = {"ts_utc": utc_now()}
            for key, path in XML_PATHS.items():
                el = root.find(path)
                text = el.text.strip() if el is not None and el.text else None
                row[key] = text if key == "clock" else num(text)
            row["raw_xml"] = raw.decode("iso-8859-1", "replace")
            q.put(("ip100_xml", row))
            stats["xml"] += 1
        except Exception as e:
            stats["xml_err"] += 1
            if stats["xml_err"] <= 3 or stats["xml_err"] % 60 == 0:
                log(f"xml error #{stats['xml_err']}: {e}")
        time.sleep(XML_INTERVAL)


# ------------------------------------------------------------------ main
def main():
    q = queue.Queue()
    threading.Thread(target=db_writer, args=(q,), daemon=True).start()
    time.sleep(0.5)

    if SERIAL_PORT:
        threading.Thread(target=serial_reader, args=(q,), daemon=True).start()
    if UDP_PORT:
        threading.Thread(target=udp_listener, args=(q,), daemon=True).start()
    if XML_URL:
        threading.Thread(target=xml_poller, args=(q,), daemon=True).start()
    if not (SERIAL_PORT or UDP_PORT or XML_URL):
        log("no inputs enabled; set SERIAL_PORT, UDP_PORT or XML_URL")

    while True:
        time.sleep(STATS_EVERY)
        log("stats " + "  ".join(f"{k}={v}" for k, v in stats.items()))


if __name__ == "__main__":
    main()
