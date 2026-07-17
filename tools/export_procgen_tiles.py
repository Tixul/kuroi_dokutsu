#!/usr/bin/env python3
# -*- coding: ascii -*-
"""
export_procgen_tiles.py
Export un PNG tileset unique (tileset_dongeon.png) en tiles_procgen.c/.h pour
NGPC.

Le PNG est une grille de metatiles 16x16 (chaque metatile = 2x2 NGPC tiles
8x8). Le mapping (row, col) -> nom de tile est defini ci-dessous dans
TILE_MAP. Les metatiles du PNG qui ne sont pas mappes sont ignores.

Chaque metatile produit 4 sous-tiles NGPC (TL TR BL BR) groupes par PAL_TERRAIN
ou PAL_EAU selon la table.

Run depuis la racine du projet :
    python tools/export_procgen_tiles.py
"""

import os
import sys
from collections import Counter

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow required: pip install pillow")

# ---- Paths ----
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_ROOT       = os.path.dirname(_SCRIPT_DIR)
_GRAPHX     = os.path.join(_ROOT, "GraphX")
_PNG_SRC    = r"C:\Users\wilfr\Desktop\NGPC_RAG\04_MY_PROJECTS\general sprite\procgen asset test\tileset_dongeon.png"
_OUT_C      = os.path.join(_GRAPHX, "tiles_procgen.c")
_OUT_H      = os.path.join(_GRAPHX, "tiles_procgen.h")

TILE_BASE = 310  # VRAM layout : 256..309 = player_topdown, 310+ = procgen bg

# ---- Palette groups ----
# 0 = PAL_FLOOR : sol, portes, escalier, vide, tonneau (tons olives + noir)
# 1 = PAL_WALL  : murs exterieurs + interieurs (tons gris + noir)
# 2 = PAL_EAU   : eau + ponts (reserve, desactive dans menu_kuroi_dokutsu)
# Alias : PAL_TERRAIN = PAL_FLOOR pour retro-compat avec le code module.
GROUP_NAMES = ["PAL_FLOOR", "PAL_WALL", "PAL_EAU"]

# ---- Mapping (row, col) du tileset_dongeon.png vers nom de tile + groupe
#       et cellules de fallback pour les noms manquants dans le schema.
#
# Grille du PNG : 4 rows x 7 cols de metatiles 16x16. Seules les cellules
# listees ici sont lues. Les cellules non listees sont ignorees (deco perso,
# objets futurs, etc.).
#
# Donnees utilisateur (2026-04-24) :
#   00 coin sup gauche, 01 mur haut, 02 coin sup droit
#   10 mur gauche, 11 sol principal, 12 mur droite
#   20 angle inf gauche, 21 mur inf, 22 angle inf droit
#   31 escalier
#   03 porte haut, 13 porte gauche, 23 porte bas, 33 porte haut encore (variante)
#   14 sol 2
#   34 vide
#   05 angle mur int sup gauche
#   15 mur int gauche

# Mapping definitif : (row, col) -> (TILE_NAME, palette_group_idx)
# group 0 = PAL_FLOOR (olive), group 1 = PAL_WALL (gris), group 2 = PAL_EAU
TILE_MAP = {
    # --- Murs exterieurs (palette gris) ---
    (0, 0): ("TILE_WALL_EXT_NW", 1),
    (0, 1): ("TILE_WALL_EXT_N",  1),
    (0, 2): ("TILE_WALL_EXT_NE", 1),
    (1, 0): ("TILE_WALL_EXT_W",  1),
    (1, 2): ("TILE_WALL_EXT_E",  1),
    (2, 0): ("TILE_WALL_EXT_SW", 1),
    (2, 1): ("TILE_WALL_EXT_S",  1),
    (2, 2): ("TILE_WALL_EXT_SE", 1),
    # --- Murs interieurs (palette gris ; partiels : juste NW et W) ---
    (0, 5): ("TILE_WALL_INT_NW", 1),
    (1, 5): ("TILE_WALL_INT_W",  1),
    # --- Sol (palette olive) ---
    (1, 1): ("TILE_GROUND_1", 0),
    (1, 4): ("TILE_GROUND_2", 0),
    # --- Vide / escalier (palette olive) ---
    (3, 4): ("TILE_VIDE",      0),
    (3, 1): ("TILE_ESCALIER",  0),
    # --- Portes (palette gris, car le chambranle est en pierre comme les murs) ---
    (0, 3): ("TILE_DOOR_N",   1),
    (1, 3): ("TILE_DOOR_W",   1),
    (2, 3): ("TILE_DOOR_S",   1),
    (3, 3): ("TILE_DOOR_N2",  1),  # variante "porte haut encore"
}

# Fallbacks pour les noms du schema procgen absents du TILE_MAP.
# Le module ngpc_dungeongen reference ces defines ; si l'utilisateur ne les a
# pas encore dessines, on les aliase sur une tile existante proche.
FALLBACKS = {
    "TILE_GROUND_3":     "TILE_GROUND_2",
    "TILE_WALL_INT_N":   "TILE_WALL_EXT_N",
    "TILE_WALL_INT_S":   "TILE_WALL_EXT_S",
    "TILE_WALL_INT_E":   "TILE_WALL_INT_W",
    "TILE_WALL_INT_NE":  "TILE_WALL_INT_NW",
    "TILE_WALL_INT_SW":  "TILE_WALL_INT_NW",
    "TILE_WALL_INT_SE":  "TILE_WALL_INT_NW",
    "TILE_EAU_H":        "TILE_GROUND_2",
    "TILE_EAU_V":        "TILE_GROUND_2",
    "TILE_PONT_H":       "TILE_GROUND_1",
    "TILE_PONT_V":       "TILE_GROUND_1",
    "TILE_VIDE_BORD":    "TILE_VIDE",
    "TILE_TONNEAU":      "TILE_GROUND_1",
}

# Ordre d'emission (metatiles = 4 NGPC tiles chacun, stride 4 entre metatiles).
TILE_ORDER = [
    "TILE_GROUND_1", "TILE_GROUND_2", "TILE_GROUND_3",
    "TILE_WALL_EXT_N",  "TILE_WALL_EXT_S",
    "TILE_WALL_EXT_W",  "TILE_WALL_EXT_E",
    "TILE_WALL_EXT_NW", "TILE_WALL_EXT_NE",
    "TILE_WALL_EXT_SW", "TILE_WALL_EXT_SE",
    "TILE_WALL_INT_N",  "TILE_WALL_INT_S",
    "TILE_WALL_INT_W",  "TILE_WALL_INT_E",
    "TILE_WALL_INT_NW", "TILE_WALL_INT_NE",
    "TILE_WALL_INT_SW", "TILE_WALL_INT_SE",
    "TILE_EAU_H",       "TILE_EAU_V",
    "TILE_PONT_H",      "TILE_PONT_V",
    "TILE_VIDE",        "TILE_VIDE_BORD",
    "TILE_TONNEAU",     "TILE_ESCALIER",
    # Extensions hors schema standard (portes)
    "TILE_DOOR_N",      "TILE_DOOR_S",
    "TILE_DOOR_W",      "TILE_DOOR_E",
]
# Fallback pour TILE_DOOR_S/E si non fournis (utilise une autre tile porte).
FALLBACKS.setdefault("TILE_DOOR_E", "TILE_DOOR_W")

# ---------------------------------------------------------------------------

def _rgba_to_rgb444(px):
    """Convert (r,g,b,a) 0-255 to (r,g,b) 0-15 ; alpha<128 -> transparent (-1,-1,-1)."""
    r, g, b, a = px
    if a < 128:
        return (-1, -1, -1)
    return (r >> 4, g >> 4, b >> 4)

def _crop_metatile(im, row, col):
    """Return the 16x16 metatile at grid (row, col) as a list of 4 sub-tiles,
    each sub-tile = 64 RGB444 pixels (TL/TR/BL/BR)."""
    x0 = col * 16
    y0 = row * 16
    sub_tiles = []
    for sy in (0, 8):
        for sx in (0, 8):
            px_list = []
            for y in range(8):
                for x in range(8):
                    px_list.append(_rgba_to_rgb444(im.getpixel((x0 + sx + x, y0 + sy + y))))
            sub_tiles.append(px_list)
    return sub_tiles  # 4 sous-tiles dans l'ordre TL, TR, BL, BR

def _pixels_to_2bpp_words(pixels, pal_idx):
    """Convert 64 RGB444 pixels to 8 u16 tile-words (NGPC 2bpp format).
    NGPC packing : MSB = leftmost pixel. Pixel x=0 -> bits 14-15,
    x=7 -> bits 0-1. Inverser (LSB first) provoque mirror horizontal. """
    words = []
    for y in range(8):
        w = 0
        for x in range(8):
            px = pixels[y * 8 + x]
            if px == (-1, -1, -1):
                idx = 0  # transparent -> index 0
            else:
                idx = pal_idx.get(px, 0)
            w |= (idx & 3) << (14 - x * 2)
        words.append(w)
    return words

def _build_palette(all_pixels, group_name):
    """Build a 4-color palette from a list of RGB444 pixel tuples (ignoring -1,-1,-1 transparent)."""
    counts = Counter(p for p in all_pixels if p != (-1, -1, -1))
    # NGPC palette index 0 is always transparent ; indexes 1-3 are user colors.
    top = [c for c, _ in counts.most_common(3)]
    if len(top) > 3:
        top = top[:3]
    palette_map = {(-1, -1, -1): 0}  # transparent -> 0
    # Reserve index 1..3 for the top colors
    pal_rgb = [(0, 0, 0), (0, 0, 0), (0, 0, 0), (0, 0, 0)]
    for i, c in enumerate(top):
        palette_map[c] = i + 1
        pal_rgb[i + 1] = c
    # Map any other colors by nearest (index 1..3)
    def _dist(a, b):
        return (a[0]-b[0])**2 + (a[1]-b[1])**2 + (a[2]-b[2])**2
    all_unique = set(p for p in all_pixels if p != (-1, -1, -1))
    for c in all_unique:
        if c not in palette_map:
            best = min(top, key=lambda t: _dist(c, t)) if top else (0, 0, 0)
            palette_map[c] = palette_map[best]
    if len(all_unique) > 3:
        print("  WARNING: %s has %d visible colors, reduced to top 3 + nearest mapping."
              % (group_name, len(all_unique)))
    return palette_map, pal_rgb

def _rgb444_c(rgb):
    r, g, b = rgb
    return "RGB(%d,%d,%d)" % (r, g, b)

# ---------------------------------------------------------------------------

def main():
    print("=== export_procgen_tiles ===")
    print("  Root   : %s" % _ROOT)
    print("  GraphX : %s" % _GRAPHX)
    print("  Src PNG: %s" % _PNG_SRC)
    print()

    if not os.path.isfile(_PNG_SRC):
        sys.exit("ERROR: tileset PNG not found: %s" % _PNG_SRC)

    im = Image.open(_PNG_SRC).convert("RGBA")
    im_w, im_h = im.size
    grid_cols = im_w // 16
    grid_rows = im_h // 16
    print("PNG grid: %d cols x %d rows of 16x16 metatiles" % (grid_cols, grid_rows))

    # 1. Load mapped metatiles
    print("Loading metatiles from TILE_MAP...")
    # metatile_by_name : TILE_NAME -> (group_idx, [sub_tile_pixels x4])
    metatile_by_name = {}
    for (row, col), (name, group) in sorted(TILE_MAP.items()):
        if row >= grid_rows or col >= grid_cols:
            sys.exit("ERROR: mapping (%d,%d)=%s is out of grid (%dx%d)."
                     % (row, col, name, grid_cols, grid_rows))
        sub_tiles = _crop_metatile(im, row, col)
        metatile_by_name[name] = (group, sub_tiles)
        print("  (%d,%d) -> %s" % (row, col, name))

    # 2. Resolve fallbacks : add missing names by aliasing to existing ones
    print()
    print("Resolving fallbacks...")
    for name in TILE_ORDER:
        if name in metatile_by_name:
            continue
        alias = FALLBACKS.get(name)
        if alias is None:
            sys.exit("ERROR: '%s' is in TILE_ORDER but not mapped and no fallback defined."
                     % name)
        if alias not in metatile_by_name:
            sys.exit("ERROR: fallback '%s' -> '%s' : alias not provided either."
                     % (name, alias))
        metatile_by_name[name] = metatile_by_name[alias]
        print("  %s -> fallback to %s" % (name, alias))

    # 3. Build palettes per group
    print()
    print("Building palettes...")
    pal_by_group = {}
    for g_idx, g_name in enumerate(GROUP_NAMES):
        all_px = []
        for name in TILE_ORDER:
            grp, subs = metatile_by_name[name]
            if grp == g_idx:
                for sub in subs:
                    all_px.extend(sub)
        palette_map, pal_rgb = _build_palette(all_px, g_name)
        pal_by_group[g_idx] = (palette_map, pal_rgb)
        rgb_hex = " ".join("0x%01X%01X%01X" % (r, g, b) for (r, g, b) in pal_rgb[1:])
        print("  %s: idx1..3 = %s" % (g_name, rgb_hex))

    # 4. Emit tile words in TILE_ORDER
    print()
    all_words = []
    for name in TILE_ORDER:
        grp, subs = metatile_by_name[name]
        palette_map, _ = pal_by_group[grp]
        for sub in subs:
            all_words.extend(_pixels_to_2bpp_words(sub, palette_map))

    n_metatiles = len(TILE_ORDER)
    n_ngpc_tiles = n_metatiles * 4
    n_words = len(all_words)
    print("Emitting %d metatiles = %d NGPC tiles = %d words..." % (n_metatiles, n_ngpc_tiles, n_words))

    # --- Write .c ---
    with open(_OUT_C, "w") as f:
        f.write("/* tiles_procgen.c - AUTO-GENERATED par tools/export_procgen_tiles.py */\n")
        f.write("/* Source : tileset_dongeon.png (single PNG, 4x7 metatile grid)      */\n")
        f.write("/* DO NOT EDIT manually - re-run le script apres changement du PNG.  */\n\n")
        f.write('#include "ngpc_types.h"\n')
        f.write('#include "../src/core/ngpc_hw.h"\n\n')
        f.write("const u16 TILES_PROCGEN_COUNT = %du;\n\n" % n_words)
        f.write("const u16 NGP_FAR TILES_PROCGEN[] = {\n")
        # emit 8 words per line
        for i in range(0, len(all_words), 8):
            chunk = all_words[i:i + 8]
            f.write("    " + ", ".join("0x%04X" % w for w in chunk) + ",")
            # add a comment indicating which TILE_* this line falls into
            # each metatile = 32 words (4 sub-tiles * 8 words)
            mt_idx = i // 32
            if i % 32 == 0 and mt_idx < n_metatiles:
                f.write(" /* %s */" % TILE_ORDER[mt_idx])
            f.write("\n")
        f.write("};\n")
        # palette emission (as C defines in .h -- here just palette data block)
    print("Wrote %s" % _OUT_C)

    # --- Write .h ---
    with open(_OUT_H, "w") as f:
        f.write("/* tiles_procgen.h - AUTO-GENERATED par tools/export_procgen_tiles.py */\n")
        f.write("/* Source : tileset_dongeon.png                                      */\n\n")
        f.write("#ifndef TILES_PROCGEN_H\n#define TILES_PROCGEN_H\n\n")
        f.write('#include "ngpc_types.h"\n\n')
        f.write("/* VRAM slot du premier tile procgen (slots 0-127 = font). */\n")
        f.write("#define TILE_BASE      %du\n\n" % TILE_BASE)
        f.write("/* Stride entre metatiles : 4 NGPC tiles par metatile 16x16. */\n")
        f.write("#define MTILE_STRIDE   4u\n\n")
        f.write("/* ---- Index des metatiles (TL tile du bloc 2x2). ---- */\n")
        for idx, name in enumerate(TILE_ORDER):
            f.write("#define %-22s (TILE_BASE + %du)\n" % (name, idx * 4))
        f.write("\n")
        f.write("/* ---- Palette groups ---- */\n")
        for idx, g in enumerate(GROUP_NAMES):
            f.write("#define %-14s %du\n" % (g, idx))
        f.write("/* Alias retro-compat : le module referencait PAL_TERRAIN */\n")
        f.write("#define PAL_TERRAIN    PAL_FLOOR\n")
        f.write("\n")
        f.write("/* ---- Palette colors ---- */\n")
        for idx, g in enumerate(GROUP_NAMES):
            _, pal_rgb = pal_by_group[idx]
            for ci in range(4):
                f.write("#define %s_C%d  %s\n" % (g, ci, _rgb444_c(pal_rgb[ci])))
            f.write("\n")
        f.write("/* Alias retro-compat PAL_TERRAIN -> PAL_FLOOR */\n")
        for ci in range(4):
            f.write("#define PAL_TERRAIN_C%d  PAL_FLOOR_C%d\n" % (ci, ci))
        f.write("\n")
        f.write("extern const u16 NGP_FAR TILES_PROCGEN[];\n")
        f.write("extern const u16          TILES_PROCGEN_COUNT;\n\n")
        f.write("#endif /* TILES_PROCGEN_H */\n")
    print("Wrote %s" % _OUT_H)

    print()
    print("Done. %d metatiles (%d NGPC tiles) exported." % (n_metatiles, n_ngpc_tiles))
    print("  Load in C:")
    print("    ngpc_gfx_load_tiles_at(TILES_PROCGEN, TILES_PROCGEN_COUNT, %du);" % TILE_BASE)

if __name__ == "__main__":
    main()
