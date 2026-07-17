#!/usr/bin/env python3
# -*- coding: ascii -*-
"""
export_door_lock.py

Convertit :
  - GraphX/door_sheet.png   (64x16 RGBA, 4 frames de 16x16) -> door_sheet_tiles.c/.h
  - GraphX/declencheur.png  (16x16 RGBA)                    -> declencheur_tiles.c/.h

Format NGPC 2bpp. Palettes dediees (slots SCR1 5 et 6).

Door palette (4 couleurs, marron+stone) :
  C0 = 0x0000 transparent
  C1 = 0x0000 noir (outline)
  C2 = 0x0588 beige stone
  C3 = 0x037A marron wood

Trigger palette (4 couleurs, gris) :
  C0 = 0x0000 transparent
  C1 = 0x0000 noir (outline)
  C2 = 0x0555 gris fonce
  C3 = 0x0878 gris clair

Chaque tile 16x16 = 4 sous-tiles 8x8 (TL TR BL BR), 8 u16 chacun.
Door : 4 frames * 4 tiles = 16 tiles = 128 u16.
Trigger : 1 tile metatile = 4 tiles = 32 u16.

Usage :
    python tools/export_door_lock.py
"""

import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow required: pip install pillow")

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_ROOT       = os.path.dirname(_SCRIPT_DIR)
_GRAPHX     = os.path.join(_ROOT, "GraphX")

_DOOR_PNG = os.path.join(_GRAPHX, "door_sheet.png")
_TRIG_PNG = os.path.join(_GRAPHX, "declencheur.png")

_DOOR_C = os.path.join(_GRAPHX, "door_sheet_tiles.c")
_DOOR_H = os.path.join(_GRAPHX, "door_sheet_tiles.h")
_TRIG_C = os.path.join(_GRAPHX, "declencheur_tiles.c")
_TRIG_H = os.path.join(_GRAPHX, "declencheur_tiles.h")

DOOR_PAL = [0x0000, 0x0000, 0x0588, 0x037A]
TRIG_PAL = [0x0000, 0x0000, 0x0555, 0x0878]


def _door_color_to_idx(rgba):
    """Door PNG colors -> 4 indices.
    Source palette :
      (0,0,0)         noir       -> C1 outline
      (154,145,104)   beige clr  -> C2 stone (r-b=50)
      (122,116,82)    beige drk  -> C2 stone (r-b=40, merged)
      (141,106,45)    marron mid -> C3 wood  (r-b=96)
      (182,136,58)    marron lt  -> C3 wood  (r-b=124, merged)

    Discrimination : wood vs stone via la saturation rouge/bleu.
    Stone (gris-beige peu sature) a r-b autour de 40-50.
    Wood (marron sature) a r-b >= 96. Seuil 75 separe proprement.
    """
    r, g, b, a = rgba
    if a < 128:
        return 0
    if r < 16 and g < 16 and b < 16:
        return 1  # noir outline
    # marron sature : R > G > B avec gap r-b > 75
    if r > g and g > b and (r - b) > 75:
        return 3
    # par defaut : stone beige (faible saturation, ou non-marron)
    return 2


def _trig_color_to_idx(rgba):
    """Trigger PNG colors -> 4 indices.
      (0,0,0)         noir   -> C1 outline
      (89,86,82)      drk    -> C2 gris fonce
      (132,126,135)   light  -> C3 gris clair
    """
    r, g, b, a = rgba
    if a < 128:
        return 0
    if r < 16 and g < 16 and b < 16:
        return 1
    # luminance approximation
    lum = (r + g + b) // 3
    if lum < 100:
        return 2
    return 3


def _tile_to_words(tile_pixels):
    """tile_pixels = 64 indices (0..3), retourne 8 u16 NGPC."""
    words = []
    for row in range(8):
        w = 0
        for col in range(8):
            idx = tile_pixels[row * 8 + col]
            shift = (7 - col) * 2
            w |= (idx & 0x3) << shift
        words.append(w)
    return words


def _emit_metatile(im, sx, sy, color_fn):
    """Decoupe 16x16 a (sx,sy) en 4 sous-tiles 8x8 (TL TR BL BR),
    retourne 32 u16 (4 * 8)."""
    out = []
    for oy in (0, 8):
        for ox in (0, 8):
            pixels = []
            for y in range(8):
                for x in range(8):
                    pixels.append(color_fn(im.getpixel((sx + ox + x, sy + oy + y))))
            out.extend(_tile_to_words(pixels))
    return out


def _emit_metatile_rot90ccw(im, sx, sy, color_fn):
    """Idem _emit_metatile mais avec rotation 90 deg CCW appliquee au metatile
    complet (pour les exits W/E ou la porte verticale doit etre orientee
    horizontalement). NGPC HW ne fait pas de rotation : on doit re-baker
    les tiles avec les pixels deja rotates.

    Mapping global 16x16 : new_pixel(X, Y) = old_pixel(15-Y, X).
    Apres rotation, le layout 2x2 du metatile devient :
        new_TL = old_TR (sub-tile contenu rotate 90 CCW)
        new_TR = old_BR
        new_BL = old_TL
        new_BR = old_BL
    """
    out = []
    for new_oy in (0, 8):
        for new_ox in (0, 8):
            pixels = []
            for new_iy in range(8):
                for new_ix in range(8):
                    # Coords pixels dans le nouveau metatile (16x16)
                    nx = new_ox + new_ix
                    ny = new_oy + new_iy
                    # Mapping global CCW : old_pixel(15-Y, X)
                    ox = 15 - ny
                    oy = nx
                    pixels.append(color_fn(im.getpixel((sx + ox, sy + oy))))
            out.extend(_tile_to_words(pixels))
    return out


def _emit_door():
    if not os.path.isfile(_DOOR_PNG):
        sys.exit("ERROR: %s not found" % _DOOR_PNG)
    im = Image.open(_DOOR_PNG).convert("RGBA")
    w, h = im.size
    if (w, h) != (64, 16):
        sys.exit("ERROR: door_sheet.png is %dx%d, expected 64x16" % (w, h))

    # Variante N/S : tiles tel quel (le code C utilise vflip HW pour S).
    ns_words = []
    for fi in range(4):
        ns_words.extend(_emit_metatile(im, fi * 16, 0, _door_color_to_idx))
    assert len(ns_words) == 128

    # Variante W/E : rotation 90 deg CCW appliquee au baking (le code C
    # utilise hflip HW pour E vs W). NGPC HW ne fait pas de rotation, donc
    # on doit re-baker les pixels rotates.
    we_words = []
    for fi in range(4):
        we_words.extend(_emit_metatile_rot90ccw(im, fi * 16, 0, _door_color_to_idx))
    assert len(we_words) == 128

    with open(_DOOR_H, "w") as f:
        f.write("/* Generated by tools/export_door_lock.py - do not edit */\n")
        f.write("#ifndef DOOR_SHEET_TILES_H\n")
        f.write("#define DOOR_SHEET_TILES_H\n\n")
        f.write('#include "ngpc_types.h"\n\n')
        f.write("/* 4 frames x 4 tiles 8x8 = 16 tiles NGPC = 128 u16.\n")
        f.write(" * Frame 0 = porte fermee, frame 3 = juste avant fully open\n")
        f.write(" * (la frame fully open est la porte existante TILE_U_DOOR_N).\n")
        f.write(" * Layout : pour frame F, tile base = frame_base + F * 4.\n")
        f.write(" *\n")
        f.write(" * _ns = orientation source (verticale, pour exits N/S).\n")
        f.write(" *       Pour S : vflip HW.\n")
        f.write(" * _we = rotation 90 deg CCW (horizontale, pour exits W/E).\n")
        f.write(" *       Pour E : hflip HW. */\n")
        f.write("extern const u16 NGP_FAR door_sheet_tiles_ns[128];\n")
        f.write("extern const u16 NGP_FAR door_sheet_tiles_we[128];\n\n")
        f.write("/* Palette 4 couleurs SCR1 : C0 transp, C1 noir, C2 stone, C3 wood. */\n")
        f.write("extern const u16 NGP_FAR door_sheet_palette[4];\n\n")
        f.write("#endif /* DOOR_SHEET_TILES_H */\n")

    def _emit_array(f, name, words):
        f.write("const u16 NGP_FAR %s[128] = {\n" % name)
        for ti in range(16):
            f.write("    /* tile %2d */ " % ti)
            chunk = words[ti * 8:(ti + 1) * 8]
            f.write(", ".join("0x%04X" % w for w in chunk))
            f.write(",\n")
        f.write("};\n\n")

    with open(_DOOR_C, "w") as f:
        f.write("/* Generated by tools/export_door_lock.py - do not edit */\n")
        f.write('#include "door_sheet_tiles.h"\n\n')
        _emit_array(f, "door_sheet_tiles_ns", ns_words)
        _emit_array(f, "door_sheet_tiles_we", we_words)
        f.write("const u16 NGP_FAR door_sheet_palette[4] = {\n    ")
        f.write(", ".join("0x%04X" % c for c in DOOR_PAL))
        f.write("\n};\n")

    print("Wrote %s" % _DOOR_C)


def _emit_trigger():
    if not os.path.isfile(_TRIG_PNG):
        sys.exit("ERROR: %s not found" % _TRIG_PNG)
    im = Image.open(_TRIG_PNG).convert("RGBA")
    w, h = im.size
    if (w, h) != (16, 16):
        sys.exit("ERROR: declencheur.png is %dx%d, expected 16x16" % (w, h))

    all_words = _emit_metatile(im, 0, 0, _trig_color_to_idx)
    assert len(all_words) == 32

    with open(_TRIG_H, "w") as f:
        f.write("/* Generated by tools/export_door_lock.py - do not edit */\n")
        f.write("#ifndef DECLENCHEUR_TILES_H\n")
        f.write("#define DECLENCHEUR_TILES_H\n\n")
        f.write('#include "ngpc_types.h"\n\n')
        f.write("/* 4 tiles 8x8 NGPC (TL TR BL BR) = 32 u16. */\n")
        f.write("extern const u16 NGP_FAR declencheur_tiles[32];\n\n")
        f.write("/* Palette 4 couleurs SCR1 : C0 transp, C1 noir, C2 gris fonce, C3 gris clair. */\n")
        f.write("extern const u16 NGP_FAR declencheur_palette[4];\n\n")
        f.write("#endif /* DECLENCHEUR_TILES_H */\n")

    with open(_TRIG_C, "w") as f:
        f.write("/* Generated by tools/export_door_lock.py - do not edit */\n")
        f.write('#include "declencheur_tiles.h"\n\n')
        f.write("const u16 NGP_FAR declencheur_tiles[32] = {\n")
        for ti in range(4):
            f.write("    /* tile %d */ " % ti)
            chunk = all_words[ti * 8:(ti + 1) * 8]
            f.write(", ".join("0x%04X" % w for w in chunk))
            f.write(",\n")
        f.write("};\n\n")
        f.write("const u16 NGP_FAR declencheur_palette[4] = {\n    ")
        f.write(", ".join("0x%04X" % c for c in TRIG_PAL))
        f.write("\n};\n")

    print("Wrote %s" % _TRIG_C)


def main():
    _emit_door()
    _emit_trigger()


if __name__ == "__main__":
    main()
