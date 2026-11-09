#!/usr/bin/env python3
"""Bench replay: send logged Rainwise packets to a station-role RAK4631 over USB.

Reads raw_hex from a rainwise_logger.py CSV and writes each 32-byte packet to
the serial port every --interval seconds, like the station does over the air.
The station board echoes what it received and sent; that text is printed.

  python lora/replay_packets.py /dev/ttyACM0 rainwise_log.csv
  python lora/replay_packets.py /dev/ttyACM0 rainwise_log.csv --interval 0.5 --loop

Rain-fix bench test: --tip-every adds a fake rain tip every N packets, and
--restart-after fakes a station power cycle (a pause, then the rain counter
back at 0). Byte 14 and the CRC are recomputed.

  python lora/replay_packets.py ports/station rainwise_log.csv --tip-every 3 --restart-after 20

Requires pyserial (https://pyserial.readthedocs.io/).
"""

import argparse
import csv
import time

import serial


def crc16_xmodem(data):
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
            crc &= 0xFFFF
    return crc


def set_rain(pkt, count):
    """Byte 13 = rain counter, byte 14 = 255 - counter, CRC over bytes 1-30."""
    p = bytearray(pkt)
    p[12] = count & 0xFF
    p[13] = 255 - p[12]
    crc = crc16_xmodem(p[:30])
    p[30], p[31] = crc >> 8, crc & 0xFF
    return bytes(p)


def packets(path):
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            pkt = bytes.fromhex(row["raw_hex"])
            if len(pkt) == 32:
                yield pkt


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("port", help="station board's USB serial port, e.g. /dev/ttyACM0 or COM5")
    ap.add_argument("csv", help="CSV written by rainwise_logger.py")
    ap.add_argument("--interval", type=float, default=2.0, help="seconds between packets (default 2)")
    ap.add_argument("--loop", action="store_true", help="start over at the end of the file")
    ap.add_argument("--tip-every", type=int, default=0, metavar="N",
                    help="add one fake rain tip every N packets")
    ap.add_argument("--restart-after", type=int, default=0, metavar="N",
                    help="after N packets, fake a station restart: pause, then rain counter at 0")
    ap.add_argument("--restart-gap", type=float, default=40.0, metavar="S",
                    help="seconds of silence for the fake restart (default 40)")
    args = ap.parse_args()
    fake_rain = args.tip_every or args.restart_after
    tips = 0     # fake tips added so far
    offset = 0   # subtracted after the fake restart, so the counter starts at 0

    with serial.Serial(args.port, 115200, timeout=0) as port:
        sent = 0
        while True:
            for pkt in packets(args.csv):
                if fake_rain:
                    if args.restart_after and sent == args.restart_after:
                        offset = pkt[12] + tips
                        print(f"\n--- fake station restart: {args.restart_gap:g} s pause, "
                              f"rain counter back to 0 ---", flush=True)
                        time.sleep(args.restart_gap)
                    if args.tip_every and sent and sent % args.tip_every == 0:
                        tips += 1
                    pkt = set_rain(pkt, pkt[12] + tips - offset)
                    print(f"[rain counter sent: {pkt[12]}]", flush=True)
                port.write(pkt)
                sent += 1
                deadline = time.monotonic() + args.interval
                while time.monotonic() < deadline:
                    echo = port.read(4096)
                    if echo:
                        print(echo.decode("ascii", "replace"), end="", flush=True)
                    time.sleep(0.05)
            if not args.loop:
                break
        print(f"\nreplayed {sent} packets")


if __name__ == "__main__":
    main()
