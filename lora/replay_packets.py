#!/usr/bin/env python3
"""Bench replay: send logged Rainwise packets to a station-role RAK4631 over USB.

Reads raw_hex from a rainwise_logger.py CSV and writes each 32-byte packet to
the serial port every --interval seconds, like the station does over the air.
The station board echoes what it received and sent; that text is printed.

  python lora/replay_packets.py /dev/ttyACM0 rainwise_log.csv
  python lora/replay_packets.py /dev/ttyACM0 rainwise_log.csv --interval 0.5 --loop

Requires pyserial (https://pyserial.readthedocs.io/).
"""

import argparse
import csv
import time

import serial


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
    args = ap.parse_args()

    with serial.Serial(args.port, 115200, timeout=0) as port:
        sent = 0
        while True:
            for pkt in packets(args.csv):
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
