#!/usr/bin/env python3
"""Capture the M5Paper page canvas over serial and save it as a PNG.

Protocol (firmware `shot` command, SNAP V2):
    #SNAP V2 540 960 4BPP <len> <crc32hex>\n
    <len bytes of packed 4bpp, high nibble first, row stride (w+1)/2>
    \n#SNAP END <crc32hex>\n

The device idle-sleeps between UI activity and silently drops serial input
arriving mid-slice (~50% at a 180 ms retry cadence), so this tool repeats
the command until the firmware acknowledges it.

Usage:
    python tools/preview/dump_screen.py --port COM9 --cmd "page 0" --out shot.png
    python tools/preview/dump_screen.py --port COM9 --reset   # boot + capture

Requires Pillow for PNG output.
"""
from __future__ import annotations

import argparse
import re
import sys
import time
import zlib
from pathlib import Path

import serial
from PIL import Image

W, H = 540, 960
STRIDE = (W + 1) // 2
PAYLOAD = STRIDE * H
HEADER_RE = re.compile(rb"#SNAP V2 (\d+) (\d+) 4BPP (\d+) ([0-9A-Fa-f]{8})\r?\n")
END_TAIL = b"#SNAP END"


def open_port(port: str, reset: bool) -> serial.Serial:
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 0.3
    ser.write_timeout = 5
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


def grab_status(ser: serial.Serial, seconds: float = 6.0) -> str:
    """Repeat status until acked; idle sleep eats some of the first tries."""
    deadline = time.time() + seconds
    buf = bytearray()
    last = 0.0
    while time.time() < deadline:
        n = ser.read(4096)
        if n:
            buf.extend(n)
            if re.search(rb"v\d+\.\d+\.\d+ page=", bytes(buf)):
                return bytes(buf).decode("utf-8", "replace")
        now = time.time()
        if now - last >= 0.18:
            send_line(ser, "status")
            last = now
    raise RuntimeError("no status reply")


def save(out_png: Path, raw: bytes, crc: int) -> None:
    """Expand packed 4bpp (white=15) to gray8 and write the PNG."""
    if ".." in out_png.parts:
        raise SystemExit(f"--out must not contain '..': {out_png}")
    scale = [v * 17 for v in range(16)]
    px = bytearray(W * H)
    pos = 0
    for y in range(H):
        base = y * STRIDE
        for x in range(W):
            b = raw[base + (x >> 1)]
            nib = (b >> 4) if (x & 1) == 0 else (b & 0x0F)
            px[pos] = scale[nib]
            pos += 1
    out_png.parent.mkdir(parents=True, exist_ok=True)
    out_png.write_bytes(b"")  # reserve validated target before PIL write
    Image.frombytes("L", (W, H), bytes(px)).save(out_png)
    print(f"saved {out_png} ({PAYLOAD} B frame, crc {crc:08X})", flush=True)


def grab_shot(ser: serial.Serial, out_png: Path, timeout: float = 120.0) -> None:
    """Send `shot` repeatedly until #SNAP V2 shows up, then stream the frame.

    Once a header has been seen we stop resending so we never queue a second
    dump behind the first one; a corrupt frame is retried whole.
    """
    ser.reset_input_buffer()
    hdr, hdr_at = None, -1
    buf = bytearray()
    started = time.time()
    last_send = 0.0

    while time.time() < started + timeout:
        chunk = ser.read(16384)
        if chunk:
            buf.extend(chunk)

        if hdr is None:
            m = HEADER_RE.search(bytes(buf))
            if m:
                w, h, length = int(m.group(1)), int(m.group(2)), int(m.group(3))
                if (w, h) != (W, H):
                    raise RuntimeError(f"unexpected geometry {w}x{h}")
                if length != PAYLOAD:
                    raise RuntimeError(f"unexpected payload {length}")
                crc_expected = int(m.group(4), 16)
                hdr, hdr_at = m.group(0), m.end()

        if hdr is not None and len(buf) >= hdr_at + PAYLOAD:
            raw = bytes(buf[hdr_at : hdr_at + PAYLOAD])
            crc_got = zlib.crc32(raw) & 0xFFFFFFFF
            if crc_got != crc_expected:
                print(f"[snap] CRC mismatch got={crc_got:08X} want={crc_expected:08X},"
                      f" retrying whole transfer", flush=True)
                del buf[:]
                hdr, hdr_at = None, -1
                continue
            deadline = time.time() + 3
            while END_TAIL not in bytes(buf[hdr_at + PAYLOAD:]) and time.time() < deadline:
                more = ser.read(4096)
                if more:
                    buf.extend(more)
            if END_TAIL not in bytes(buf[hdr_at + PAYLOAD:]):
                print("[snap] warning: end marker never seen; CRC ok anyway",
                      flush=True)
            save(out_png, raw, crc_got)
            return

        # header not here yet — keep the request alive; during transfer,
        # resending would queue a duplicate dump, so header-phase only.
        if hdr is None and time.time() - last_send >= 0.18:
            send_line(ser, "shot")
            last_send = time.time()

    raise RuntimeError(f"snap timed out after {timeout}s, rx={len(buf)}B")


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", default="COM9")
    p.add_argument("--reset", action="store_true", help="pulse EN before capture")
    p.add_argument("--out", default=str(Path(__file__).with_name("live.png")))
    p.add_argument("--cmd", default="", help="device command before shot (e.g. about)")
    args = p.parse_args()
    out_png = Path(args.out)
    if ".." in out_png.parts:
        raise SystemExit(f"--out must not contain '..': {args.out}")

    ser = open_port(args.port, args.reset)
    try:
        if args.reset:
            print("reset, waiting 10s for boot...", flush=True)
            sys.stdout.write(read_until(ser, time.time() + 10).decode("utf-8", "replace"))
            sys.stdout.flush()
        print("--- status ---", flush=True)
        print(grab_status(ser), flush=True)
        if args.cmd:
            send_line(ser, args.cmd)
            time.sleep(7)          # full-quality repaint takes 1-2 s (+ retries)
            print(read_until(ser, time.time() + 3).decode("utf-8", "replace"), flush=True)
        print("--- shot ---", flush=True)
        grab_shot(ser, out_png)
    finally:
        ser.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
