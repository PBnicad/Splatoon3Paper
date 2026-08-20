#!/usr/bin/env python3
"""Dump M5Paper page canvas over serial (`shot`) to a PNG.

Opens COM without holding EN/DTR reset. Optional --reset pulses RTS.
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

import serial
from PIL import Image

W, H = 540, 960
PAYLOAD = W * H
MAGIC = b"P5\n540 960\n255\n"


def open_port(port: str, reset: bool) -> serial.Serial:
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 0.3
    ser.write_timeout = 2
    ser.dsrdtr = False
    ser.rtscts = False
    ser.dtr = False
    ser.rts = False
    ser.open()
    if reset:
        time.sleep(0.05)
        ser.rts = True
        ser.dtr = False
        time.sleep(0.12)
        ser.rts = False
        ser.dtr = False
    return ser


def read_until(ser: serial.Serial, deadline: float) -> bytes:
    buf = b""
    while time.time() < deadline:
        chunk = ser.read(4096)
        if chunk:
            buf += chunk
    return buf


def send_line(ser: serial.Serial, line: str) -> None:
    ser.write(line.encode("ascii") + b"\n")
    ser.flush()


def grab_status(ser: serial.Serial, seconds: float = 3.0) -> str:
    send_line(ser, "status")
    deadline = time.time() + seconds
    buf = b""
    while time.time() < deadline:
        buf += ser.read(4096)
    return buf.decode("utf-8", "replace")


def grab_shot(ser: serial.Serial, out_png: Path, timeout: float = 90.0) -> None:
    # discard pending
    ser.reset_input_buffer()
    send_line(ser, "shot")
    deadline = time.time() + timeout
    buf = bytearray()
    while time.time() < deadline:
        chunk = ser.read(16384)
        if chunk:
            buf += chunk
            idx = buf.find(MAGIC)
            if idx >= 0 and len(buf) >= idx + len(MAGIC) + PAYLOAD:
                raw = bytes(buf[idx + len(MAGIC) : idx + len(MAGIC) + PAYLOAD])
                img = Image.frombytes("L", (W, H), raw)
                img.save(out_png)
                print(f"saved {out_png} ({len(raw)} px)", flush=True)
                return
        elif buf.find(MAGIC) < 0 and time.time() > deadline - timeout + 8:
            # no header yet after 8s — keep waiting until timeout
            pass
    raise RuntimeError(
        f"shot timed out, got {len(buf)} bytes, magic={'yes' if MAGIC in buf else 'no'}"
    )


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--port", default="COM9")
    p.add_argument("--reset", action="store_true")
    p.add_argument("--out", default=str(Path(__file__).with_name("live.png")))
    p.add_argument("--cmd", default="", help="extra command before shot (e.g. page 0)")
    args = p.parse_args()

    ser = open_port(args.port, args.reset)
    try:
        if args.reset:
            print("reset, waiting 8s for boot...", flush=True)
            boot = read_until(ser, time.time() + 8)
            sys.stdout.write(boot.decode("utf-8", "replace"))
            sys.stdout.flush()
        print("--- status ---", flush=True)
        print(grab_status(ser), flush=True)
        if args.cmd:
            send_line(ser, args.cmd)
            time.sleep(2)
            print(read_until(ser, time.time() + 2).decode("utf-8", "replace"), flush=True)
        print("--- shot ---", flush=True)
        grab_shot(ser, Path(args.out))
    finally:
        ser.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
