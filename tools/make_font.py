#!/usr/bin/env python3
"""Generate 4bpp grayscale bitmap fonts for the M5Paper firmware.

Output format ("SNF1", little-endian):
    magic "SNF1" | u16 version | u16 pixel_size | u16 line_gap | u16 reserved
    u32 glyph_count | u32 index_offset | u32 data_offset
    index: glyph_count x { u32 codepoint, u32 data_off }  (sorted by codepoint)
    glyph: u8 w, u8 h, i8 xoff, i8 yoff, u8 adv, u8 flags, u8 r0, u8 r1,
           then ceil(w*h/2) bytes of 4bpp alpha (high nibble first)

Glyphs are rasterized with PIL from a CJK TTF/OTF/TTC. The charset is the
union of: ASCII printable, curated UI strings (tools/ui-strings.txt), every
character appearing in the splatoon3.ink zh-CN locale file (fixture), and the
site's zh-CN UI labels. Sizes: 24 (body, full charset), 40 (headings, full
charset), 96 (clock, digits+punctuation only).

Usage:
    python tools/make_font.py [--font PATH] [--outdir data] [--preview]
"""

import argparse
import json
import struct
import sys
from pathlib import Path

from PIL import ImageFont

ROOT = Path(__file__).resolve().parent.parent

FONT_CANDIDATES = [
    ROOT / "tools" / "fonts" / "NotoSansSC-Regular.otf",
    Path(r"C:\Windows\Fonts\msyh.ttc"),
]

SIZES_FULL = [24, 40]
SIZE_CLOCK = 96
CLOCK_CHARS = "0123456789:%+-./ ℃"

MAGIC = b"SNF1"
VERSION = 1


def collect_charset() -> set[int]:
    chars = set(chr(c) for c in range(0x20, 0x7F))
    ui = ROOT / "tools" / "ui-strings.txt"
    chars |= set(ui.read_text(encoding="utf-8"))

    locale_path = ROOT / "worker" / "test" / "fixtures" / "locale-zh-CN.json"
    if locale_path.exists():
        data = json.loads(locale_path.read_text(encoding="utf-8"))
        stack = [data]
        while stack:
            v = stack.pop()
            if isinstance(v, dict):
                stack.extend(v.values())
            elif isinstance(v, list):
                stack.extend(v)
            elif isinstance(v, str):
                chars |= set(v)
    else:
        print("WARN: locale fixture missing, charset limited to UI strings")

    i18n_path = ROOT / "tools" / "ui-zh-CN.json"
    if i18n_path.exists():
        data = json.loads(i18n_path.read_text(encoding="utf-8"))
        stack = [data]
        while stack:
            v = stack.pop()
            if isinstance(v, dict):
                stack.extend(v.values())
            elif isinstance(v, list):
                stack.extend(v)
            elif isinstance(v, str):
                chars |= set(v)

    chars.discard("\n")
    chars.discard("\r")
    chars.discard("\t")
    return {ord(c) for c in chars}


def rasterize(font: ImageFont.FreeTypeFont, ch: str):
    """Return (w, h, xoff, yoff, adv, alpha bytes 4bpp-packed)."""
    try:
        length = font.getlength(ch)
    except Exception:
        return None
    mask = font.getmask(ch, mode="L")
    w, h = mask.size
    adv = max(1, int(round(length)))
    if w == 0 or h == 0:
        return (0, 0, 0, 0, adv, b"")
    pixels = list(mask)
    packed = bytearray()
    for i in range(0, len(pixels), 2):
        hi = pixels[i] >> 4
        lo = pixels[i + 1] >> 4 if i + 1 < len(pixels) else 0
        packed.append((hi << 4) | lo)
    # PIL renders with anchor 'la': origin = left of advance, top of ascender
    return (w, h, 0, 0, adv, bytes(packed))


def build_font(font_path: Path, px: int, charset: list) -> bytes:
    font = ImageFont.truetype(str(font_path), px, index=0)
    entries = []
    blob = bytearray()
    for cp in charset:
        g = rasterize(font, chr(cp))
        if g is None:
            continue
        w, h, xoff, yoff, adv, data = g
        if w > 255 or h > 255 or adv > 255 or not (-128 <= xoff <= 127):
            print(f"WARN: glyph U+{cp:04X} out of metric range at {px}px, skipped")
            continue
        off = len(blob)
        blob += struct.pack("<BBbbBBBB", w, h, xoff, yoff, adv, 0, 0, 0)
        blob += data
        entries.append((cp, off))
    header_size = 24
    index_offset = header_size
    data_offset = header_size + 8 * len(entries)
    out = bytearray()
    out += MAGIC
    out += struct.pack("<HHHH", VERSION, px, px // 5, 0)
    out += struct.pack("<III", len(entries), index_offset, data_offset)
    for cp, off in entries:
        out += struct.pack("<II", cp, off)
    out += blob
    assert len(out) == data_offset + len(blob)
    return bytes(out)


def decode_preview(bin_path: Path, text: str, out_png: Path):
    """Decode an SNF1 file and render `text` (wrapped on \\n) to a PNG —
    round-trip validation of the generated binary."""
    raw = bin_path.read_bytes()
    assert raw[:4] == MAGIC, "bad magic"
    ver, px, line_gap, _ = struct.unpack_from("<HHHH", raw, 4)
    count, index_off, data_off = struct.unpack_from("<III", raw, 12)
    index = {}
    for i in range(count):
        cp, off = struct.unpack_from("<II", raw, index_off + 8 * i)
        index[cp] = off
    from PIL import Image

    lines = text.split("\n")
    line_h = px + line_gap + 4
    img = Image.new("L", (900, line_h * len(lines) + 20), 255)
    pix = img.load()
    for li, line in enumerate(lines):
        x = 8
        ybase = 10 + li * line_h
        for ch in line:
            cp = ord(ch)
            if cp not in index:
                cp = ord("?")
            off = data_off + index[cp]
            w, h, xoff, yoff, adv = struct.unpack_from("<BBbbB", raw, off)
            data = raw[off + 8 :]
            for gy in range(h):
                for gx in range(w):
                    i = gy * w + gx
                    byte = data[i // 2]
                    a = (byte >> 4) if i % 2 == 0 else (byte & 0xF)
                    if a:
                        px_x, px_y = x + xoff + gx, ybase + yoff + gy
                        if 0 <= px_x < img.width and 0 <= px_y < img.height:
                            pix[px_x, px_y] = min(pix[px_x, px_y], 255 - a * 17)
            x += adv
    img.save(out_png)
    print(f"preview -> {out_png} ({ver=}, {px}px, {count} glyphs)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", type=Path, default=None)
    ap.add_argument("--outdir", type=Path, default=ROOT / "data")
    ap.add_argument("--preview", action="store_true")
    args = ap.parse_args()

    font_path = args.font or next((p for p in FONT_CANDIDATES if p.exists()), None)
    if not font_path:
        sys.exit("no CJK font found; pass --font PATH")
    print(f"source font: {font_path}")

    charset = sorted(collect_charset())
    print(f"charset: {len(charset)} codepoints")

    outdir: Path = args.outdir
    outdir.mkdir(parents=True, exist_ok=True)
    manifest = [f"source: {font_path}", f"charset: {len(charset)}"]

    for px in SIZES_FULL:
        blob = build_font(font_path, px, charset)
        out = outdir / f"font{px}.bin"
        out.write_bytes(blob)
        manifest.append(f"font{px}.bin: {len(blob)} bytes")
        print(f"font{px}.bin: {len(blob)} bytes")
        if args.preview:
            sample = "斯普拉遁3 日程 12:34\n一般比赛 占地对战\n竹蛏疏洪道 · 贝见亭\n真格蛤蜊 X比赛 100%\n鲑鱼跑 大型跑 横纲 还剩 1天3小时"
            decode_preview(out, sample, ROOT / "tools" / "preview" / f"preview-font{px}.png")

    clock_set = sorted({ord(c) for c in CLOCK_CHARS})
    blob = build_font(font_path, SIZE_CLOCK, clock_set)
    out = outdir / f"font{SIZE_CLOCK}.bin"
    out.write_bytes(blob)
    manifest.append(f"font{SIZE_CLOCK}.bin: {len(blob)} bytes")
    print(f"font{SIZE_CLOCK}.bin: {len(blob)} bytes")

    (outdir / "font-manifest.txt").write_text("\n".join(manifest) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
