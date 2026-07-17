#!/usr/bin/env python3
# -*- coding: ascii -*-
"""
export_tileset_unit.py

Exporte les 18 PNG 16x16 de GraphX/tilset_unit/ vers tiles_unit.c/h.

Format NGPC 2bpp, 3 palettes :
- PAL_FLOOR : sol + vide       (fond gris, couleurs figees)
- PAL_WALL  : murs + portes + escalier + pilier (fond olive, couleurs figees)
- PAL_DECO  : decors vase/totem (quantises depuis les PNG, pour SCR2)

Chaque PNG = 1 metatile 16x16 = 4 tiles NGPC 8x8 (TL TR BL BR).

Les tiles sont emises dans l'ordre TILE_ORDER, consecutivement depuis
TILE_BASE (VRAM slot 310). Chaque metatile prend 4 slots.

Usage : depuis la racine du projet :
    python tools/export_tileset_unit.py
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
_TILE_SRC   = os.path.join(_GRAPHX, "tilset_unit")
_OUT_C      = os.path.join(_GRAPHX, "tiles_unit.c")
_OUT_H      = os.path.join(_GRAPHX, "tiles_unit.h")

TILE_BASE = 310  # VRAM : 32-127=font, 128-171=sprites jeu, 256-309=player, 310+=tileset_unit

# ---- Palettes fixes ----
# Alignement pixel-identique avec salle_01_palettes[] qui rend correctement
# chez l'utilisateur. Structure memoire emise :
#   Slot 0 (PAL_WALL) : [0x000, 0x000,     0x699,    0x577]
#                         C0     C1=noir  C2=olive  C3=olive
#                                         clair    fonce
#   Slot 1 (PAL_FLOOR): [0x000, 0x999,    0x666,    0x000]
#                         C0    C1=gris   C2=gris   C3=noir
#                               clair     fonce
# Les tile data sont encodees avec le color_to_idx ci-dessous.
PAL_FIXED = {
    "PAL_WALL": {
        # Slot 0 : murs + doors + pillar + stair (olive)
        (0, 0, 0): 1,  # noir (outline)       -> C1 = 0x000
        (9, 9, 6): 2,  # olive clair (pierre) -> C2 = 0x699
        (7, 7, 5): 3,  # olive fonce (mortier)-> C3 = 0x577
    },
    "PAL_FLOOR": {
        # Slot 1 : sol + void (gris)
        (9, 9, 9): 1,  # gris clair -> C1 = 0x999
        (6, 6, 6): 2,  # gris fonce -> C2 = 0x666
        (0, 0, 0): 3,  # noir       -> C3 = 0x000
    },
    "PAL_DECO": {
        # SCR2 slot 1 : decors vase/totem.
        # 3 couleurs hand-picked qui couvrent les 2 PNGs sans dominance
        # par les noirs du vase. Tous les autres pixels seront mappes
        # NEAREST sur l'une de ces 3.
        (7, 4, 3):    1,   # brun moyen (vase corps + totem mid)
        (13, 12, 11): 2,   # cream (totem highlight + vase highlight)
        (1, 1, 1):    3,   # dark (outlines des deux)
    },
}

# Slots de palette assignes a chaque groupe.
PAL_SLOTS = {
    "PAL_WALL":  0,  # SCR1 slot 0 (matches salle_01 palette 0)
    "PAL_FLOOR": 1,  # SCR1 slot 1 (matches salle_01 palette 1)
    "PAL_DECO":  1,  # SCR2 slot 1 (pas 0 pour ne pas conflit avec font)
}

# Couleur C0 pour chaque palette.
PAL_C0 = {
    "PAL_WALL":  (0, 0, 0),
    "PAL_FLOOR": (0, 0, 0),
    "PAL_DECO":  (0, 0, 0),  # SCR2 transparent
}

# ---- Mapping des tiles : ordre d'emission + palette cible ----
# L'ordre definit aussi les defines TILE_U_* via l'offset * 4 depuis TILE_BASE.
TILE_ORDER = [
    # -- SCR1 / PAL_FLOOR --
    ("floor_1",       "PAL_FLOOR"),
    ("floor_2",       "PAL_FLOOR"),
    ("void_fill",     "PAL_FLOOR"),
    ("void_edge_n",   "PAL_FLOOR"),
    ("void_edge_w",   "PAL_FLOOR"),
    # -- SCR1 / PAL_WALL --
    ("wall_outer_n",  "PAL_WALL"),
    ("wall_outer_w",  "PAL_WALL"),
    ("wall_outer_nw", "PAL_WALL"),
    ("wall_outer_ne", "PAL_WALL"),
    ("wall_inner_n",  "PAL_WALL"),
    ("wall_inner_w",  "PAL_WALL"),
    ("wall_inner_nw", "PAL_WALL"),
    ("door_n",        "PAL_WALL"),
    ("door_w",        "PAL_WALL"),
    ("pillar",        "PAL_WALL"),
    ("stair",         "PAL_WALL"),
    # -- SCR2 / PAL_DECO (quantise) --
    ("deco_totem",    "PAL_DECO"),
    ("deco_vase",     "PAL_DECO"),
]

# ---------------------------------------------------------------------------

def _rgb444(px):
    """RGBA -> (r4, g4, b4) RGB444 tuple, ou None si transparent."""
    r, g, b, a = px
    if a < 128:
        return None
    return (r >> 4, g >> 4, b >> 4)

def _load_metatile(png_name):
    """Charge 1 PNG 16x16 et retourne 4 sous-tiles (TL, TR, BL, BR).
    Chaque sous-tile = liste de 64 tuples RGB444 (ou None pour transparent)."""
    path = os.path.join(_TILE_SRC, png_name + ".png")
    if not os.path.isfile(path):
        sys.exit("ERROR: tile PNG not found: %s" % path)
    im = Image.open(path).convert("RGBA")
    w, h = im.size
    if w != 16 or h != 16:
        sys.exit("ERROR: %s is %dx%d, expected 16x16" % (png_name, w, h))
    sub_tiles = []
    for sy in (0, 8):
        for sx in (0, 8):
            px_list = []
            for y in range(8):
                for x in range(8):
                    px_list.append(_rgb444(im.getpixel((sx + x, sy + y))))
            sub_tiles.append(px_list)
    return sub_tiles  # [TL, TR, BL, BR]

def _build_deco_palette(deco_subs):
    """PAL_DECO hand-pick : couleurs FIXES (PAL_FIXED["PAL_DECO"]) + nearest
    mapping pour tous les autres pixels rencontres dans les PNG.

    Ancienne version utilisait Counter.most_common(3) sur les pixels
    cumules totem+vase. Probleme : le vase a beaucoup de pixels noirs
    (60+) qui ecrasaient les highlights cream du totem (41) -> top3 =
    {cream, dark, dark} sans aucun brun -> pixels bruns du vase
    fall-through nearest=dark -> vase rendait tout noir = N&B perdu.

    Maintenant, palette = brown + cream + dark explicitement.
    """
    fixed_map = PAL_FIXED["PAL_DECO"]   # {(r,g,b): idx}
    # Construit pal_rgb [C0, C1, C2, C3]
    pal_rgb = [PAL_C0["PAL_DECO"], (0, 0, 0), (0, 0, 0), (0, 0, 0)]
    for color, idx in fixed_map.items():
        pal_rgb[idx] = color

    palette_map = {None: 0}
    palette_map.update(fixed_map)

    # Nearest mapping pour TOUS les pixels presents dans les PNG mais
    # non listes dans fixed_map.
    def dist(a, b):
        return (a[0]-b[0])**2 + (a[1]-b[1])**2 + (a[2]-b[2])**2

    encountered = set()
    for subs in deco_subs:
        for sub in subs:
            for px in sub:
                if px is not None:
                    encountered.add(px)

    fixed_colors = list(fixed_map.keys())
    extras = 0
    for c in encountered:
        if c not in palette_map:
            best = min(fixed_colors, key=lambda t: dist(c, t))
            palette_map[c] = fixed_map[best]
            extras += 1
    if extras:
        print("  NOTE: PAL_DECO a %d couleurs hand-picked + %d colors "
              "mappees nearest depuis les PNG"
              % (len(fixed_map), extras))
    return palette_map, pal_rgb

def _pixels_to_words(pixels, palette_map):
    """64 pixels -> 8 u16 words NGPC 2bpp, MSB first (x=0 -> bits 14-15)."""
    words = []
    for y in range(8):
        w = 0
        for x in range(8):
            px = pixels[y * 8 + x]
            idx = palette_map.get(px, 0) if px is not None else 0
            w |= (idx & 3) << (14 - x * 2)
        words.append(w)
    return words

def _rgb_c(rgb):
    r, g, b = rgb
    return "RGB(%d,%d,%d)" % (r, g, b)

# ---------------------------------------------------------------------------

def main():
    print("=== export_tileset_unit ===")
    print("  Src dir : %s" % _TILE_SRC)
    print("  Output  : %s" % _OUT_C)
    print()

    # 1. Charger tous les metatiles
    metatiles = {}  # name -> [sub_tiles x4]
    for name, pal_group in TILE_ORDER:
        metatiles[name] = _load_metatile(name)

    # 2. Construire la palette PAL_DECO depuis les pixels des decors
    deco_subs = [metatiles[name] for name, grp in TILE_ORDER if grp == "PAL_DECO"]
    deco_map, deco_rgb = _build_deco_palette(deco_subs)

    # 3. Palettes pour chaque groupe (map + rgb layout C0..C3)
    palettes = {}  # pal_group -> (palette_map, [c0,c1,c2,c3])
    for pal_name, color_to_idx in PAL_FIXED.items():
        # Construire pal_rgb en inversant color_to_idx
        pal_rgb = [PAL_C0[pal_name], (0, 0, 0), (0, 0, 0), (0, 0, 0)]
        for color, idx in color_to_idx.items():
            pal_rgb[idx] = color
        palette_map = {None: 0}
        palette_map.update(color_to_idx)
        palettes[pal_name] = (palette_map, pal_rgb)
    palettes["PAL_DECO"] = (deco_map, deco_rgb)

    # 4. Emettre les tile words dans l'ordre TILE_ORDER
    all_words = []
    for name, pal_group in TILE_ORDER:
        pmap, _ = palettes[pal_group]
        for sub in metatiles[name]:
            all_words.extend(_pixels_to_words(sub, pmap))

    n_metatiles = len(TILE_ORDER)
    n_ngpc_tiles = n_metatiles * 4
    n_words = len(all_words)
    print("Emission : %d metatiles = %d tiles NGPC = %d words" % (n_metatiles, n_ngpc_tiles, n_words))

    # 5. Ecriture du .c
    with open(_OUT_C, "w") as f:
        f.write("/* tiles_unit.c - AUTO-GENERATED par tools/export_tileset_unit.py */\n")
        f.write("/* Source : GraphX/tilset_unit/*.png (18 metatiles 16x16)        */\n")
        f.write("/* DO NOT EDIT manually - re-run le script apres modif PNG.      */\n\n")
        f.write('#include "ngpc_types.h"\n')
        f.write('#include "../src/core/ngpc_hw.h"\n\n')
        f.write("const u16 TILES_UNIT_COUNT = %du;\n\n" % n_words)
        f.write("const u16 NGP_FAR TILES_UNIT[] = {\n")
        for i in range(0, len(all_words), 8):
            chunk = all_words[i:i + 8]
            line = "    " + ", ".join("0x%04X" % w for w in chunk) + ","
            # Commentaire : nom du metatile courant (chaque 32 words = 1 metatile)
            mt_idx = i // 32
            if i % 32 == 0 and mt_idx < n_metatiles:
                line += " /* %s */" % TILE_ORDER[mt_idx][0]
            f.write(line + "\n")
        f.write("};\n")
    print("Wrote %s" % _OUT_C)

    # 6. Ecriture du .h
    with open(_OUT_H, "w") as f:
        f.write("/* tiles_unit.h - AUTO-GENERATED par tools/export_tileset_unit.py */\n\n")
        f.write("#ifndef TILES_UNIT_H\n#define TILES_UNIT_H\n\n")
        f.write('#include "ngpc_types.h"\n\n')
        f.write("/* VRAM slot du premier tile (TL de la 1ere metatile). */\n")
        f.write("#define TILE_U_BASE       %du\n\n" % TILE_BASE)
        f.write("/* Une metatile = 4 tiles NGPC 8x8 consecutives. */\n")
        f.write("#define TILE_U_STRIDE     4u\n\n")

        f.write("/* ---- Index metatile (tile NGPC TL du bloc 2x2) ---- */\n")
        for idx, (name, _) in enumerate(TILE_ORDER):
            define_name = "TILE_U_" + name.upper()
            f.write("#define %-22s (TILE_U_BASE + %du)\n" % (define_name, idx * 4))
        f.write("\n")

        f.write("/* ---- Slots palette ---- */\n")
        f.write("#define PAL_WALL     %du    /* SCR1 slot : murs + doors + pillar + stair (olive) */\n" % PAL_SLOTS["PAL_WALL"])
        f.write("#define PAL_FLOOR    %du    /* SCR1 slot : sol + void (gris) */\n" % PAL_SLOTS["PAL_FLOOR"])
        f.write("#define PAL_DECO     %du    /* SCR2 slot : decors vase/totem (non-0 pour ne pas etre ecrase par font) */\n\n" % PAL_SLOTS["PAL_DECO"])

        f.write("/* ---- Couleurs palette (RGB444) ---- */\n")
        for pal_name in ("PAL_FLOOR", "PAL_WALL", "PAL_DECO"):
            _, pal_rgb = palettes[pal_name]
            for ci in range(4):
                f.write("#define %s_C%d  %s\n" % (pal_name, ci, _rgb_c(pal_rgb[ci])))
            f.write("\n")

        f.write("extern const u16 NGP_FAR TILES_UNIT[];\n")
        f.write("extern const u16          TILES_UNIT_COUNT;\n\n")
        f.write("#endif /* TILES_UNIT_H */\n")
    print("Wrote %s" % _OUT_H)

    print()
    print("Done. Chargement runtime :")
    print("  ngpc_gfx_load_tiles_at(TILES_UNIT, TILES_UNIT_COUNT, TILE_U_BASE);")

if __name__ == "__main__":
    main()
