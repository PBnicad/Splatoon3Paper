#!/usr/bin/env python3
"""Union every CJK-containing string literal from src/ into
tools/ui-strings.txt so make_font.py covers the full on-screen charset.
Run after adding/changing UI text in the firmware, before make_font.py."""

import glob
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LITERAL = re.compile(r'"([^"\\\n]*[^\x00-\x7f][^"\\\n]*)"')
EXTRA_PUNCT = {"…", "·", "▲", "~", "％", "◆", "◇"}

found = set()
for f in sorted(glob.glob(str(ROOT / "src" / "*.cpp")) + glob.glob(str(ROOT / "src" / "*.h"))):
    for m in LITERAL.finditer(Path(f).read_text(encoding="utf-8")):
        found.add(m.group(1))
found |= EXTRA_PUNCT

path = ROOT / "tools" / "ui-strings.txt"
cur = {l for l in path.read_text(encoding="utf-8").splitlines() if l}
cur |= found
path.write_text("\n".join(sorted(cur)) + "\n", encoding="utf-8", newline="\n")
print(f"literals found: {len(found)}; total lines: {len(cur)}")
for s in sorted(found):
    print(" +", s)
