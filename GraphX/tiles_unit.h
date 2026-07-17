/* tiles_unit.h - AUTO-GENERATED par tools/export_tileset_unit.py */

#ifndef TILES_UNIT_H
#define TILES_UNIT_H

#include "ngpc_types.h"

/* VRAM slot du premier tile (TL de la 1ere metatile). */
#define TILE_U_BASE       310u

/* Une metatile = 4 tiles NGPC 8x8 consecutives. */
#define TILE_U_STRIDE     4u

/* ---- Index metatile (tile NGPC TL du bloc 2x2) ---- */
#define TILE_U_FLOOR_1         (TILE_U_BASE + 0u)
#define TILE_U_FLOOR_2         (TILE_U_BASE + 4u)
#define TILE_U_VOID_FILL       (TILE_U_BASE + 8u)
#define TILE_U_VOID_EDGE_N     (TILE_U_BASE + 12u)
#define TILE_U_VOID_EDGE_W     (TILE_U_BASE + 16u)
#define TILE_U_WALL_OUTER_N    (TILE_U_BASE + 20u)
#define TILE_U_WALL_OUTER_W    (TILE_U_BASE + 24u)
#define TILE_U_WALL_OUTER_NW   (TILE_U_BASE + 28u)
#define TILE_U_WALL_OUTER_NE   (TILE_U_BASE + 32u)
#define TILE_U_WALL_INNER_N    (TILE_U_BASE + 36u)
#define TILE_U_WALL_INNER_W    (TILE_U_BASE + 40u)
#define TILE_U_WALL_INNER_NW   (TILE_U_BASE + 44u)
#define TILE_U_DOOR_N          (TILE_U_BASE + 48u)
#define TILE_U_DOOR_W          (TILE_U_BASE + 52u)
#define TILE_U_PILLAR          (TILE_U_BASE + 56u)
#define TILE_U_STAIR           (TILE_U_BASE + 60u)
#define TILE_U_DECO_TOTEM      (TILE_U_BASE + 64u)
#define TILE_U_DECO_VASE       (TILE_U_BASE + 68u)

/* ---- Slots palette ---- */
#define PAL_WALL     0u    /* SCR1 slot : murs + doors + pillar + stair (olive) */
#define PAL_FLOOR    1u    /* SCR1 slot : sol + void (gris) */
#define PAL_DECO     1u    /* SCR2 slot : decors vase/totem (non-0 pour ne pas etre ecrase par font) */

/* ---- Couleurs palette (RGB444) ---- */
#define PAL_FLOOR_C0  RGB(0,0,0)
#define PAL_FLOOR_C1  RGB(9,9,9)
#define PAL_FLOOR_C2  RGB(6,6,6)
#define PAL_FLOOR_C3  RGB(0,0,0)

#define PAL_WALL_C0  RGB(0,0,0)
#define PAL_WALL_C1  RGB(0,0,0)
#define PAL_WALL_C2  RGB(9,9,6)
#define PAL_WALL_C3  RGB(7,7,5)

#define PAL_DECO_C0  RGB(0,0,0)
#define PAL_DECO_C1  RGB(7,4,3)
#define PAL_DECO_C2  RGB(13,12,11)
#define PAL_DECO_C3  RGB(1,1,1)

extern const u16 NGP_FAR TILES_UNIT[];
extern const u16          TILES_UNIT_COUNT;

#endif /* TILES_UNIT_H */
