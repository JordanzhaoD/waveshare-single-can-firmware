#!/usr/bin/env python3
"""Brute-force the Tesla 0x249 SCCMLeftStalk CRC8 (byte0) from a capture CSV.

CSV columns: ts_ms,dir,bus,id,dlc,b0..b7  (b0 = SCCM_leftStalkCrc).
Tries forward and reflected CRC8 over several input constructions, using the
"xorout is a constant XOR" trick to keep the search at 256*256 per form.
"""
import sys
from pathlib import Path

CSV = Path(sys.argv[1]) if len(sys.argv) > 1 else None
if not CSV or not CSV.exists():
    print("usage: crack_stalk_crc.py <capture.csv>")
    sys.exit(1)

# --- parse 0x249 (585) frames, dedupe by (b1,b2,b3) -> b0 -----------------
pairs = {}
total = 0
for line in CSV.read_text(errors="ignore").splitlines()[1:]:
    p = line.split(",")
    if len(p) < 13:
        continue
    if p[3] != "585":
        continue
    b = [int(x) for x in p[5:13]]
    total += 1
    key = (b[1], b[2], b[3])
    pairs.setdefault(key, b[0])

print(f"0x249 frames: {total}, unique (b1,b2,b3): {len(pairs)}")
samples = [(k, v) for k, v in pairs.items()]


def fwd_table(poly):
    t = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = ((c << 1) ^ poly) & 0xFF if (c & 0x80) else (c << 1) & 0xFF
        t.append(c)
    return t


def rev_table(poly):  # poly already reflected
    t = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = (c >> 1) ^ poly if (c & 1) else (c >> 1)
        t.append(c & 0xFF)
    return t


def make_inputs(b1, b2, b3):
    return {
        "[b1,b2,b3]": [b1, b2, b3],
        "[b3,b2,b1]": [b3, b2, b1],
        "[0,b1,b2,b3]": [0, b1, b2, b3],
        "[b1]": [b1],
        "[id_hi,id_lo,b1,b2,b3]": [0x02, 0x49, b1, b2, b3],
    }


def crc(table, data, init, reflected):
    c = init
    if reflected:
        for d in data:
            c = table[(c ^ d) & 0xFF]
    else:
        for d in data:
            c = table[(c ^ d) & 0xFF]
    return c


solutions = []
input_names = list(make_inputs(0, 0, 0).keys())

for reflected in (False, True):
    tablefn = rev_table if reflected else fwd_table
    for iname in input_names:
        # precompute input lists per sample for this construction
        inputs = [(make_inputs(*k)[iname], b0) for k, b0 in samples]
        for poly in range(256):
            table = tablefn(poly)
            for init in range(256):
                xorset = None
                ok = True
                for data, b0 in inputs:
                    raw = crc(table, data, init, reflected)
                    x = raw ^ b0
                    if xorset is None:
                        xorset = x
                    elif x != xorset:
                        ok = False
                        break
                if ok:
                    solutions.append((reflected, iname, poly, init, xorset))

print(f"\nfound {len(solutions)} solution(s):")
for reflected, iname, poly, init, xorout in solutions[:40]:
    kind = "reflected" if reflected else "forward"
    print(f"  {kind} poly=0x{poly:02X} init=0x{init:02X} "
          f"xorout=0x{xorout:02X} input={iname}")
