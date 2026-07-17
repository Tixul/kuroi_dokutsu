#!/usr/bin/env python3
"""Quantize a PNG so each 8x8 tile has <=3 visible colors + transparency.
Used to fix hand-edited assets that exceed NGPC palette constraints.

For each tile that exceeds 3 visible RGB444 colors:
  - Count occurrences of each color
  - Keep the top-3 most-common
  - Snap minority pixels to nearest top-3 (Euclidean distance in RGB)

Usage:
  python tools/quantize_tile_ngpc.py input.png output.png
"""
from __future__ import annotations
import sys
from collections import Counter
from PIL import Image


def quantize(in_path: str, out_path: str) -> None:
    im = Image.open(in_path).convert("RGBA")
    w, h = im.size
    px = im.load()
    fixed = 0
    for ty in range(h // 8):
        for tx in range(w // 8):
            # Collect 8-bit colors in this tile (key = RGB444 tuple)
            counts: Counter[tuple[int, int, int]] = Counter()
            for y in range(ty * 8, ty * 8 + 8):
                for x in range(tx * 8, tx * 8 + 8):
                    r, g, b, a = px[x, y]
                    if a < 128:
                        continue
                    key = (r >> 4, g >> 4, b >> 4)
                    counts[key] += 1
            if len(counts) <= 3:
                continue
            # Take top-3
            top3 = [c for c, _ in counts.most_common(3)]
            # Snap each minority pixel to nearest of top3
            for y in range(ty * 8, ty * 8 + 8):
                for x in range(tx * 8, tx * 8 + 8):
                    r, g, b, a = px[x, y]
                    if a < 128:
                        continue
                    key = (r >> 4, g >> 4, b >> 4)
                    if key in top3:
                        continue
                    # Find closest top-3 by Euclidean distance
                    best = top3[0]
                    best_d = 1 << 30
                    for c in top3:
                        d = (c[0] - key[0]) ** 2 + (c[1] - key[1]) ** 2 + (c[2] - key[2]) ** 2
                        if d < best_d:
                            best_d = d
                            best = c
                    # Snap back to 8-bit (replicate 4 bits to 8)
                    nr = (best[0] << 4) | best[0]
                    ng = (best[1] << 4) | best[1]
                    nb = (best[2] << 4) | best[2]
                    px[x, y] = (nr, ng, nb, a)
            fixed += 1
    im.save(out_path)
    print(f"Quantized {fixed} tile(s) to <=3 visible colors. Saved {out_path}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    quantize(sys.argv[1], sys.argv[2])
