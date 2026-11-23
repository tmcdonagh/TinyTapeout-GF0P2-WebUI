#!/usr/bin/env python3
"""Send a serial command to the Pico and print its response.

Auto-detects the first Raspberry Pi (VID 2e8a:) USB CDC port. Streams
output until a quiet period, so long dumps (e.g. the 'b' frame hex dump)
complete before exit.

Usage:
    python3 pico/tools/serctl.py "d"
    python3 pico/tools/serctl.py "s33"
    python3 pico/tools/serctl.py ""          # just listen for heartbeats
"""

import argparse
import sys
import time

try:
    import serial  # noqa: F401  (pyserial)
    from serial.tools import list_ports
except ImportError:
    sys.stderr.write("pyserial missing: python3 -m pip install --user pyserial\n")
    sys.exit(3)

PICO_VID = 0x2E8A


def find_port(explicit: str | None) -> str:
    if explicit:
        return explicit
    candidates = [p.device for p in list_ports.comports() if p.vid == PICO_VID]
    if not candidates:
        sys.stderr.write(
            "no USB serial device with VID 2e8a found "
            "(is the Pico attached to WSL? see usbipd attach)\n"
        )
        sys.exit(1)
    if len(candidates) > 1:
        sys.stderr.write(f"multiple Pico ports {candidates}, using {candidates[0]}\n")
    return candidates[0]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("command", nargs="?", default="")
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--quiet", type=float, default=1.0,
                    help="seconds of silence that ends streaming")
    ap.add_argument("--max-wait", type=float, default=10.0,
                    help="overall streaming cap in seconds")
    args = ap.parse_args()

    port = find_port(args.port)
    try:
        with serial.Serial(port, args.baud, timeout=0.2) as ser:
            ser.write(args.command.encode() + b"\n")
            start = time.monotonic()
            last_rx = start
            while True:
                now = time.monotonic()
                if now - start > args.max_wait:
                    break
                if now - last_rx > args.quiet and now - start > args.quiet:
                    break
                chunk = ser.read(4096)
                if chunk:
                    sys.stdout.write(chunk.decode(errors="replace"))
                    sys.stdout.flush()
                    last_rx = time.monotonic()
            return 0
    except serial.SerialException as exc:
        sys.stderr.write(f"serial error on {port}: {exc}\n")
        return 2


if __name__ == "__main__":
    sys.exit(main())
