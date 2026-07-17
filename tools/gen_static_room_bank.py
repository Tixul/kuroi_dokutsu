#!/usr/bin/env python3
"""
gen_static_room_bank.py

Generate a first-pass bank of static dungeon rooms for the NGPC project.

The output is a pair of C files:
    src/static_room_bank.h
    src/static_room_bank.c

Design goals:
- room sizes in the requested range: min 20x17, max 28x27
- exits are always centered and 2 tiles wide for easy cluster assembly
- if a room exits south, the next room can reliably spawn from north
- first jet covers all useful exit masks with concrete room definitions
- some single-exit leaf rooms expose a stair socket, others contain void drops
"""

from __future__ import annotations

from collections import defaultdict, deque
from dataclasses import dataclass, field
from pathlib import Path


EXIT_N = 0x01
EXIT_E = 0x02
EXIT_S = 0x04
EXIT_W = 0x08

MIN_W = 10
MIN_H = 9
MAX_W = 14
MAX_H = 14

MAX_VARIANTS_PER_MASK = 4
INVALID_ROOM_INDEX = 0xFF

# MKD-3-A : nombre max d'anchors decor par room (cap dur ROM + RAM runtime)
MAX_DECOR_ANCHORS_PER_ROOM = 12

ROLE_NODE = "NODE"
ROLE_LEAF = "LEAF"

# MKD-6 : roles semantiques (drive le furnishing pass MKD-3)
SEM_ENTRY        = "ENTRY"          # premiere salle, ambiance ceremoniel, 0 ennemi
SEM_TRANSIT      = "TRANSIT"        # passage rapide, peu d'enemy, decor minimal
SEM_COMBAT_LIGHT = "COMBAT_LIGHT"   # 2-3 ennemis, decor moyen
SEM_COMBAT_HEAVY = "COMBAT_HEAVY"   # 4+ ennemis (cap par room cap), decor varie
SEM_TREASURE     = "TREASURE"       # item garanti, eventuellement 1-2 gardiens
SEM_SAFE         = "SAFE"           # checkpoint, 0 ennemi, decor cosy
SEM_STAIR        = "STAIR"          # leaf avec escalier, gardiens forts
SEM_SECRET       = "SECRET"         # acces conditionnel, item rare

# Mapping role semantique -> #define C (utilise par emit_*)
SEMANTIC_TO_ENUM = {
    SEM_ENTRY:        "STATIC_ROOM_SEMANTIC_ENTRY",
    SEM_TRANSIT:      "STATIC_ROOM_SEMANTIC_TRANSIT",
    SEM_COMBAT_LIGHT: "STATIC_ROOM_SEMANTIC_COMBAT_LIGHT",
    SEM_COMBAT_HEAVY: "STATIC_ROOM_SEMANTIC_COMBAT_HEAVY",
    SEM_TREASURE:     "STATIC_ROOM_SEMANTIC_TREASURE",
    SEM_SAFE:         "STATIC_ROOM_SEMANTIC_SAFE",
    SEM_STAIR:        "STATIC_ROOM_SEMANTIC_STAIR",
    SEM_SECRET:       "STATIC_ROOM_SEMANTIC_SECRET",
}

# Valeurs numeriques pour le #define (stables, ne pas reordonner sans bumping ABI)
SEMANTIC_TO_VALUE = {
    SEM_ENTRY:        0,
    SEM_TRANSIT:      1,
    SEM_COMBAT_LIGHT: 2,
    SEM_COMBAT_HEAVY: 3,
    SEM_TREASURE:     4,
    SEM_SAFE:         5,
    SEM_STAIR:        6,
    SEM_SECRET:       7,
}

CELL_FLOOR = "."
CELL_CRACK = ","
CELL_OUTER = "#"
CELL_INNER = "I"
CELL_VOID = "V"
CELL_PILLAR = "P"
CELL_TOTEM = "T"
CELL_VASE = "A"

CELL_TO_ENUM = {
    CELL_FLOOR: "STATIC_ROOM_CELL_FLOOR",
    CELL_CRACK: "STATIC_ROOM_CELL_CRACK",
    CELL_OUTER: "STATIC_ROOM_CELL_OUTER_WALL",
    CELL_INNER: "STATIC_ROOM_CELL_INNER_WALL",
    CELL_VOID: "STATIC_ROOM_CELL_VOID_DROP",
    CELL_PILLAR: "STATIC_ROOM_CELL_PILLAR",
    CELL_TOTEM: "STATIC_ROOM_CELL_DECO_TOTEM",
    CELL_VASE: "STATIC_ROOM_CELL_DECO_VASE",
}

WALKABLE_CELLS = {CELL_FLOOR, CELL_CRACK}
SOLID_CELLS = {CELL_OUTER, CELL_INNER, CELL_VOID, CELL_PILLAR, CELL_TOTEM, CELL_VASE}


@dataclass
class Room:
    name: str
    w: int
    h: int
    exits_mask: int
    role: str
    cells: list[list[str]] = field(default_factory=list)
    enemy_spawns: list[tuple[int, int]] = field(default_factory=list)
    item_spawns: list[tuple[int, int]] = field(default_factory=list)
    stair_sockets: list[tuple[int, int]] = field(default_factory=list)
    # MKD-7 : feature bits supplementaires (ENTRY_UNIQUE, BOSS_ARENA, ...)
    extra_flags: list[str] = field(default_factory=list)
    # MKD-6 : role semantique (drive furnishing MKD-3). Defaut TRANSIT.
    semantic_role: str = SEM_TRANSIT
    # MKD-3-A : positions candidates pour decor TOTEM/VASE (runtime decide).
    # Peuple par normalize_decor_anchors() apres construction.
    decor_anchors: list[tuple[int, int]] = field(default_factory=list)

    def __post_init__(self) -> None:
        if not self.cells:
            self.cells = [[CELL_FLOOR for _ in range(self.w)] for _ in range(self.h)]
            for x in range(self.w):
                self.cells[0][x] = CELL_OUTER
                self.cells[self.h - 1][x] = CELL_OUTER
            for y in range(self.h):
                self.cells[y][0] = CELL_OUTER
                self.cells[y][self.w - 1] = CELL_OUTER

    def set_cell(self, x: int, y: int, cell: str) -> None:
        self.cells[y][x] = cell

    def fill_rect(self, x: int, y: int, w: int, h: int, cell: str) -> None:
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.cells[yy][xx] = cell

    def hline(self, x0: int, x1: int, y: int, cell: str) -> None:
        for xx in range(x0, x1 + 1):
            self.cells[y][xx] = cell

    def vline(self, x: int, y0: int, y1: int, cell: str) -> None:
        for yy in range(y0, y1 + 1):
            self.cells[yy][x] = cell

    def stamp_points(self, points: list[tuple[int, int]], cell: str) -> None:
        for x, y in points:
            self.cells[y][x] = cell

    def door_cols(self) -> tuple[int, int]:
        # Porte = 1 metatile (16x16 = largeur joueur). On garde le tuple
        # (lo, hi) pour compat C struct mais lo == hi -> 1 seule cell
        # FLOOR carve, 1 seule porte dessinee au runtime.
        c = self.w // 2
        return c, c

    def door_rows(self) -> tuple[int, int]:
        r = self.h // 2
        return r, r

    def carve_exits(self) -> None:
        col_lo, col_hi = self.door_cols()
        row_lo, row_hi = self.door_rows()

        # lo == hi maintenant (porte 1-cell). On laisse les 2 ecritures
        # au cas ou un sous-classe override door_cols/door_rows pour
        # retourner deux positions distinctes.
        if self.exits_mask & EXIT_N:
            self.cells[0][col_lo] = CELL_FLOOR
            self.cells[0][col_hi] = CELL_FLOOR
        if self.exits_mask & EXIT_S:
            self.cells[self.h - 1][col_lo] = CELL_FLOOR
            self.cells[self.h - 1][col_hi] = CELL_FLOOR
        if self.exits_mask & EXIT_W:
            self.cells[row_lo][0] = CELL_FLOOR
            self.cells[row_hi][0] = CELL_FLOOR
        if self.exits_mask & EXIT_E:
            self.cells[row_lo][self.w - 1] = CELL_FLOOR
            self.cells[row_hi][self.w - 1] = CELL_FLOOR

    def rotate_cw(self, new_name: str) -> "Room":
        new_w = self.h
        new_h = self.w
        new_cells = [[CELL_FLOOR for _ in range(new_w)] for _ in range(new_h)]

        for y in range(self.h):
            for x in range(self.w):
                nx = self.h - 1 - y
                ny = x
                new_cells[ny][nx] = self.cells[y][x]

        def rot_points(points: list[tuple[int, int]]) -> list[tuple[int, int]]:
            out: list[tuple[int, int]] = []
            for x, y in points:
                out.append((self.h - 1 - y, x))
            return out

        new_mask = 0
        if self.exits_mask & EXIT_N:
            new_mask |= EXIT_E
        if self.exits_mask & EXIT_E:
            new_mask |= EXIT_S
        if self.exits_mask & EXIT_S:
            new_mask |= EXIT_W
        if self.exits_mask & EXIT_W:
            new_mask |= EXIT_N

        room = Room(
            name=new_name,
            w=new_w,
            h=new_h,
            exits_mask=new_mask,
            role=self.role,
            cells=new_cells,
            enemy_spawns=rot_points(self.enemy_spawns),
            item_spawns=rot_points(self.item_spawns),
            stair_sockets=rot_points(self.stair_sockets),
            # MKD-6/MKD-7/MKD-3-A : propage role, flags et decor anchors aux rotations
            extra_flags=list(self.extra_flags),
            semantic_role=self.semantic_role,
            decor_anchors=rot_points(self.decor_anchors),
        )
        for x in range(room.w):
            room.cells[0][x] = CELL_OUTER
            room.cells[room.h - 1][x] = CELL_OUTER
        for y in range(room.h):
            room.cells[y][0] = CELL_OUTER
            room.cells[y][room.w - 1] = CELL_OUTER
        room.carve_exits()
        return room

    def feature_flags(self) -> list[str]:
        flags: list[str] = []
        flat = [cell for row in self.cells for cell in row]
        if CELL_INNER in flat or CELL_PILLAR in flat:
            flags.append("STATIC_ROOM_FEATURE_INNER")
        if CELL_VOID in flat:
            flags.append("STATIC_ROOM_FEATURE_VOID")
        # MKD-3-A : flag DECOR si anchors OU cells legacy (les anchors
        # remplacent les cells apres normalize_decor_anchors).
        if CELL_TOTEM in flat or CELL_VASE in flat or self.decor_anchors:
            flags.append("STATIC_ROOM_FEATURE_DECOR")
        if self.stair_sockets:
            flags.append("STATIC_ROOM_FEATURE_STAIR_SOCKET")
        if self.enemy_spawns:
            flags.append("STATIC_ROOM_FEATURE_ENEMY_SPAWNS")
        if self.item_spawns:
            flags.append("STATIC_ROOM_FEATURE_ITEM_SPAWNS")
        # Extra bits (MKD-7+)
        flags.extend(self.extra_flags)
        return flags

    def mask_name(self) -> str:
        parts: list[str] = []
        if self.exits_mask & EXIT_N:
            parts.append("N")
        if self.exits_mask & EXIT_E:
            parts.append("E")
        if self.exits_mask & EXIT_S:
            parts.append("S")
        if self.exits_mask & EXIT_W:
            parts.append("W")
        return "".join(parts)


def build_leaf_stair_n() -> Room:
    # 12x11 — compact leaf avec escalier au centre-sud
    room = Room("leaf_stair_n", 12, 11, EXIT_N, ROLE_LEAF)
    room.semantic_role = SEM_STAIR
    room.fill_rect(4, 5, 4, 1, CELL_CRACK)
    room.set_cell(3, 7, CELL_VASE)
    room.set_cell(8, 7, CELL_VASE)
    room.set_cell(5, 3, CELL_TOTEM)
    room.enemy_spawns = [(3, 3), (8, 3), (5, 6)]
    room.item_spawns = [(6, 8)]
    room.stair_sockets = [(5, 8)]
    room.carve_exits()
    return room


def build_leaf_void_n() -> Room:
    # 13x11 — leaf avec trou central comme "void drop"
    room = Room("leaf_void_n", 13, 11, EXIT_N, ROLE_LEAF)
    room.semantic_role = SEM_TREASURE
    room.fill_rect(5, 5, 3, 2, CELL_VOID)
    room.fill_rect(4, 7, 5, 1, CELL_CRACK)
    room.set_cell(3, 3, CELL_TOTEM)
    room.set_cell(9, 8, CELL_VASE)
    room.enemy_spawns = [(2, 4), (10, 3), (6, 9)]
    room.item_spawns = [(2, 9)]
    room.carve_exits()
    return room


def build_corridor_ns() -> Room:
    # 11x12 — corridor vertical avec 2 pillars et cracks
    room = Room("corridor_ns", 11, 12, EXIT_N | EXIT_S, ROLE_NODE)
    room.semantic_role = SEM_TRANSIT
    room.stamp_points([(3, 4), (7, 4), (3, 7), (7, 7)], CELL_PILLAR)
    room.fill_rect(4, 5, 3, 2, CELL_CRACK)
    room.enemy_spawns = [(5, 3), (5, 8)]
    room.item_spawns = [(5, 5)]
    room.carve_exits()
    return room


def build_corner_ne() -> Room:
    # 12x11 — coin avec inner walls en L
    room = Room("corner_ne", 12, 11, EXIT_N | EXIT_E, ROLE_NODE)
    room.semantic_role = SEM_TRANSIT
    room.fill_rect(4, 4, 2, 4, CELL_INNER)
    room.hline(6, 9, 7, CELL_INNER)
    room.set_cell(8, 3, CELL_VASE)
    room.set_cell(3, 8, CELL_TOTEM)
    room.enemy_spawns = [(3, 3), (8, 5)]
    room.item_spawns = [(9, 8)]
    room.carve_exits()
    return room


def build_tee_nes() -> Room:
    # 13x12 — T shape avec inner walls verticaux
    room = Room("tee_nes", 13, 12, EXIT_N | EXIT_E | EXIT_S, ROLE_NODE)
    room.semantic_role = SEM_COMBAT_LIGHT
    room.vline(4, 3, 8, CELL_INNER)
    room.set_cell(4, 6, CELL_FLOOR)
    room.stamp_points([(8, 4), (8, 8)], CELL_PILLAR)
    room.set_cell(2, 6, CELL_TOTEM)
    room.set_cell(10, 3, CELL_VASE)
    room.fill_rect(9, 5, 3, 2, CELL_CRACK)
    room.enemy_spawns = [(6, 3), (9, 8), (3, 9)]
    room.item_spawns = [(10, 5)]
    room.carve_exits()
    return room


def build_leaf_pillared_n() -> Room:
    # 11x10 — leaf compact, double rangee de piliers (couloir cere monial)
    room = Room("leaf_pillared_n", 11, 10, EXIT_N, ROLE_LEAF)
    # Tag COMBAT_LIGHT car 3 enemy_spawns existants. Pourra etre rebascule
    # en SAFE quand MKD-3 furnishing controlera dynamiquement les enemies.
    room.semantic_role = SEM_COMBAT_LIGHT
    room.stamp_points([(2, 4), (4, 4), (6, 4), (8, 4)], CELL_PILLAR)
    room.stamp_points([(2, 7), (4, 7), (6, 7), (8, 7)], CELL_PILLAR)
    room.set_cell(5, 6, CELL_TOTEM)
    room.enemy_spawns = [(3, 3), (7, 3), (5, 8)]
    room.item_spawns = [(2, 8)]
    room.carve_exits()
    return room


def build_leaf_river_n() -> Room:
    # 13x10 — leaf coupe par une "riviere" de void, traversee par 2 ponts
    room = Room("leaf_river_n", 13, 10, EXIT_N, ROLE_LEAF)
    room.semantic_role = SEM_TREASURE
    # Ligne de void avec 2 ponts (cells FLOOR conservees)
    for xx in range(1, 12):
        if xx not in (3, 9):
            room.set_cell(xx, 5, CELL_VOID)
    room.set_cell(2, 3, CELL_TOTEM)
    room.set_cell(10, 3, CELL_VASE)
    room.fill_rect(5, 7, 3, 1, CELL_CRACK)
    room.enemy_spawns = [(5, 3), (8, 3), (5, 8)]
    room.item_spawns = [(10, 8)]
    room.carve_exits()
    return room


def build_corridor_pillars_ns() -> Room:
    # 10x14 — corridor central long, double colonne de piliers + cracks
    room = Room("corridor_pillars_ns", 10, 14, EXIT_N | EXIT_S, ROLE_NODE)
    room.semantic_role = SEM_TRANSIT
    room.stamp_points([(3, 3), (3, 6), (3, 10), (6, 3), (6, 6), (6, 10)],
                      CELL_PILLAR)
    room.fill_rect(4, 4, 2, 1, CELL_CRACK)
    room.fill_rect(4, 9, 2, 1, CELL_CRACK)
    room.set_cell(2, 7, CELL_VASE)
    room.set_cell(7, 7, CELL_VASE)
    room.enemy_spawns = [(4, 2), (4, 7), (4, 11)]
    room.item_spawns = [(5, 7)]
    room.carve_exits()
    return room


def build_corner_plaza_ne() -> Room:
    # 13x12 — coin avec plaza centrale ouverte + decors aux 4 coins
    room = Room("corner_plaza_ne", 13, 12, EXIT_N | EXIT_E, ROLE_NODE)
    room.semantic_role = SEM_COMBAT_LIGHT
    room.set_cell(2, 2, CELL_TOTEM)
    room.set_cell(10, 2, CELL_VASE)
    room.set_cell(2, 9, CELL_VASE)
    room.set_cell(10, 9, CELL_TOTEM)
    # Inner walls definissant la plaza centrale
    room.fill_rect(4, 4, 5, 1, CELL_INNER)
    room.fill_rect(4, 7, 5, 1, CELL_INNER)
    room.set_cell(6, 4, CELL_FLOOR)  # passage nord
    room.set_cell(6, 7, CELL_FLOOR)  # passage sud
    room.fill_rect(5, 5, 3, 2, CELL_CRACK)
    room.enemy_spawns = [(6, 2), (10, 5), (3, 8)]
    room.item_spawns = [(6, 5)]
    room.carve_exits()
    return room


def build_tee_void_nes() -> Room:
    # 14x13 — T avec puits central de void (force le contournement)
    room = Room("tee_void_nes", 14, 13, EXIT_N | EXIT_E | EXIT_S, ROLE_NODE)
    room.semantic_role = SEM_COMBAT_HEAVY
    room.fill_rect(6, 5, 2, 2, CELL_VOID)
    room.set_cell(2, 3, CELL_TOTEM)
    room.set_cell(11, 3, CELL_VASE)
    room.set_cell(2, 9, CELL_VASE)
    room.fill_rect(4, 8, 2, 1, CELL_CRACK)
    room.fill_rect(8, 8, 2, 1, CELL_CRACK)
    room.enemy_spawns = [(4, 2), (10, 5), (4, 10)]
    room.item_spawns = [(11, 9)]
    room.carve_exits()
    return room


def build_cross_nesw() -> Room:
    # 14x14 — carrefour avec 4 coins d'inner walls + 4 pillars centraux
    room = Room("cross_nesw", 14, 14, EXIT_N | EXIT_E | EXIT_S | EXIT_W, ROLE_NODE)
    room.semantic_role = SEM_COMBAT_HEAVY
    room.fill_rect(3, 3, 2, 2, CELL_INNER)
    room.fill_rect(9, 3, 2, 2, CELL_INNER)
    room.fill_rect(3, 9, 2, 2, CELL_INNER)
    room.fill_rect(9, 9, 2, 2, CELL_INNER)
    room.stamp_points([(6, 6), (7, 6), (6, 7), (7, 7)], CELL_PILLAR)
    room.set_cell(2, 6, CELL_TOTEM)
    room.set_cell(11, 6, CELL_TOTEM)
    room.set_cell(2, 11, CELL_VASE)
    room.set_cell(11, 2, CELL_VASE)
    room.hline(5, 8, 8, CELL_CRACK)
    room.enemy_spawns = [(6, 2), (11, 5), (6, 11), (2, 5)]
    room.item_spawns = [(6, 8)]
    room.carve_exits()
    return room


def build_entrance_unique() -> Room:
    """MKD-7 : salle d'entree distinctive (4 exits, design ceremoniel).
    Pas d'ennemi, item central, 4 totems en croix autour, 4 vases aux coins.
    Le cluster_generate prefere cette room pour le slot 0."""
    # 14x13 — 4 exits, design symetrique ceremonial
    room = Room("entrance_unique", 14, 13,
                EXIT_N | EXIT_E | EXIT_S | EXIT_W, ROLE_NODE)
    room.semantic_role = SEM_ENTRY
    # 4 totems en croix centrale (gardiens de l'entree)
    room.set_cell(6, 4, CELL_TOTEM)
    room.set_cell(8, 4, CELL_TOTEM)  # wait : col 8 hors mask center, recentrons
    # Recentrage : w=14 -> col 6 et 7 sont au centre
    # On efface pour repartir propre
    for x in range(6, 8):
        for y in range(4, 5):
            room.set_cell(x, y, CELL_FLOOR)
    room.set_cell(5, 4, CELL_TOTEM)
    room.set_cell(7, 4, CELL_TOTEM)
    room.set_cell(5, 8, CELL_TOTEM)
    room.set_cell(7, 8, CELL_TOTEM)
    # 4 vases aux coins interieurs
    room.set_cell(2, 2, CELL_VASE)
    room.set_cell(11, 2, CELL_VASE)
    room.set_cell(2, 10, CELL_VASE)
    room.set_cell(11, 10, CELL_VASE)
    # Tapis central de cracks (4x4 dalle ceremoniel)
    room.fill_rect(5, 5, 3, 3, CELL_CRACK)
    # Item central : welcome reward
    room.item_spawns = [(6, 6)]
    # Pas d'ennemi (salle safe)
    room.enemy_spawns = []
    room.extra_flags = ["STATIC_ROOM_FEATURE_ENTRY_UNIQUE"]
    room.carve_exits()
    return room


def normalize_decor_anchors(room: Room) -> None:
    """MKD-3-A : transforme chaque CELL_TOTEM/VASE en decor_anchor et remplace
    la cell par FLOOR. Le runtime decide ensuite, via seed + semantic_role,
    si l'anchor est vide / totem / vase a chaque visite de la salle.

    Plafonne a MAX_DECOR_ANCHORS_PER_ROOM (sinon emet warning + tronque)."""
    for y in range(room.h):
        for x in range(room.w):
            cell = room.cells[y][x]
            if cell == CELL_TOTEM or cell == CELL_VASE:
                room.decor_anchors.append((x, y))
                room.cells[y][x] = CELL_FLOOR

    if len(room.decor_anchors) > MAX_DECOR_ANCHORS_PER_ROOM:
        print(f"  WARN: {room.name} has {len(room.decor_anchors)} decor anchors, "
              f"truncating to {MAX_DECOR_ANCHORS_PER_ROOM}")
        room.decor_anchors = room.decor_anchors[:MAX_DECOR_ANCHORS_PER_ROOM]


def rotate_n(room: Room, count: int, names: list[str]) -> list[Room]:
    # MKD-3-A : normalise decor cells -> anchors AVANT rotation, sinon
    # les rotations propageraient les cells (statiques) et les anchors
    # (dynamiques) seraient vides.
    normalize_decor_anchors(room)
    out: list[Room] = []
    cur = room
    for idx in range(count):
        if idx == 0:
            cur.name = names[idx]
            out.append(cur)
        else:
            cur = cur.rotate_cw(names[idx])
            out.append(cur)
    return out


def walkable(cell: str) -> bool:
    return cell in WALKABLE_CELLS


def validate_room(room: Room) -> None:
    if room.w < MIN_W or room.w > MAX_W:
        raise ValueError(f"{room.name}: width {room.w} outside [{MIN_W}, {MAX_W}]")
    if room.h < MIN_H or room.h > MAX_H:
        raise ValueError(f"{room.name}: height {room.h} outside [{MIN_H}, {MAX_H}]")
    if room.exits_mask == 0:
        raise ValueError(f"{room.name}: room has no exits")

    col_lo, col_hi = room.door_cols()
    row_lo, row_hi = room.door_rows()

    exit_points: list[tuple[int, int]] = []
    if room.exits_mask & EXIT_N:
        exit_points.extend([(col_lo, 0), (col_hi, 0)])
    if room.exits_mask & EXIT_E:
        exit_points.extend([(room.w - 1, row_lo), (room.w - 1, row_hi)])
    if room.exits_mask & EXIT_S:
        exit_points.extend([(col_lo, room.h - 1), (col_hi, room.h - 1)])
    if room.exits_mask & EXIT_W:
        exit_points.extend([(0, row_lo), (0, row_hi)])

    for x, y in exit_points:
        if not walkable(room.cells[y][x]):
            raise ValueError(f"{room.name}: exit cell {(x, y)} is blocked")

    for y in range(room.h):
        for x in range(room.w):
            cell = room.cells[y][x]
            if cell not in CELL_TO_ENUM:
                raise ValueError(f"{room.name}: invalid cell '{cell}' at {(x, y)}")

    for label, points in (
        ("enemy", room.enemy_spawns),
        ("item", room.item_spawns),
        ("stair", room.stair_sockets),
        ("decor_anchor", room.decor_anchors),
    ):
        for x, y in points:
            if x < 0 or x >= room.w or y < 0 or y >= room.h:
                raise ValueError(f"{room.name}: {label} point {(x, y)} outside room")
            if not walkable(room.cells[y][x]):
                raise ValueError(f"{room.name}: {label} point {(x, y)} on blocked cell")

    start = exit_points[0]
    q: deque[tuple[int, int]] = deque([start])
    seen = {start}
    while q:
        x, y = q.popleft()
        for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if nx < 0 or nx >= room.w or ny < 0 or ny >= room.h:
                continue
            if not walkable(room.cells[ny][nx]):
                continue
            if (nx, ny) in seen:
                continue
            seen.add((nx, ny))
            q.append((nx, ny))

    for x, y in exit_points:
        if (x, y) not in seen:
            raise ValueError(f"{room.name}: exit {(x, y)} unreachable")
    for label, points in (
        ("enemy", room.enemy_spawns),
        ("item", room.item_spawns),
        ("stair", room.stair_sockets),
    ):
        for point in points:
            if point not in seen:
                raise ValueError(f"{room.name}: {label} point {point} unreachable")


# =========================================================================
# MKD-4 — Validation BFS étendue (audit non-bloquant)
# =========================================================================

# Seuils (ajustables après run initial)
MIN_STAIR_DIST_FROM_ENTRY = 5       # leaf rooms ~10-14 tiles, 5 = milieu
MIN_ENEMY_DIST_FROM_ENTRY = 2       # ennemi à 2+ steps de toute case d'entrée
SAFE_RADIUS_AROUND_ENTRY  = 1       # case 3x3 autour entrée = pas d'ennemi


def _bfs_distances(room: Room, start: tuple[int, int]) -> dict[tuple[int, int], int]:
    """BFS Manhattan distances from start to all reachable cells."""
    dist: dict[tuple[int, int], int] = {start: 0}
    q: deque[tuple[int, int]] = deque([start])
    while q:
        x, y = q.popleft()
        d = dist[(x, y)]
        for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if nx < 0 or nx >= room.w or ny < 0 or ny >= room.h:
                continue
            if not walkable(room.cells[ny][nx]):
                continue
            if (nx, ny) in dist:
                continue
            dist[(nx, ny)] = d + 1
            q.append((nx, ny))
    return dist


def _connected_without(room: Room, blocked: tuple[int, int],
                       targets: list[tuple[int, int]]) -> bool:
    """Returns True if all targets are mutually reachable when `blocked` is removed."""
    if not targets:
        return True
    if blocked in targets:
        return True  # don't test removal of a target itself
    start = targets[0]
    if start == blocked:
        return True
    seen = {start}
    q: deque[tuple[int, int]] = deque([start])
    while q:
        x, y = q.popleft()
        for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if nx < 0 or nx >= room.w or ny < 0 or ny >= room.h:
                continue
            if (nx, ny) == blocked:
                continue
            if not walkable(room.cells[ny][nx]):
                continue
            if (nx, ny) in seen:
                continue
            seen.add((nx, ny))
            q.append((nx, ny))
    return all(t in seen for t in targets)


def _entry_points(room: Room) -> list[tuple[int, int]]:
    col_lo, col_hi = room.door_cols()
    row_lo, row_hi = room.door_rows()
    pts: list[tuple[int, int]] = []
    if room.exits_mask & EXIT_N:
        pts.extend([(col_lo, 0), (col_hi, 0)])
    if room.exits_mask & EXIT_E:
        pts.extend([(room.w - 1, row_lo), (room.w - 1, row_hi)])
    if room.exits_mask & EXIT_S:
        pts.extend([(col_lo, room.h - 1), (col_hi, room.h - 1)])
    if room.exits_mask & EXIT_W:
        pts.extend([(0, row_lo), (0, row_hi)])
    # Dedup
    return list(dict.fromkeys(pts))


def audit_room(room: Room) -> list[str]:
    """Run extended validation checks. Returns list of issues (empty = OK)."""
    issues: list[str] = []
    entries = _entry_points(room)

    # 1) Mutual reachability between exits — strict, exit-to-exit
    if len(entries) >= 2:
        dist_from_first = _bfs_distances(room, entries[0])
        for e in entries[1:]:
            if e not in dist_from_first:
                issues.append(f"exit {e} not mutually reachable from {entries[0]}")

    # 2) Distance entrée → escalier
    if room.stair_sockets:
        # Distance min depuis n'importe quelle entrée
        min_dist_per_stair: list[int] = []
        for stair in room.stair_sockets:
            best = None
            for entry in entries:
                d = _bfs_distances(room, entry).get(stair)
                if d is None:
                    issues.append(f"stair {stair} unreachable from entry {entry}")
                    continue
                if best is None or d < best:
                    best = d
            if best is not None:
                min_dist_per_stair.append(best)
                if best < MIN_STAIR_DIST_FROM_ENTRY:
                    issues.append(
                        f"stair {stair} too close to entry (dist={best}, "
                        f"min={MIN_STAIR_DIST_FROM_ENTRY})"
                    )

    # 3) Enemy spawns trop proches des entrées (BFS distance)
    for enemy in room.enemy_spawns:
        too_close_entry = None
        too_close_dist = None
        for entry in entries:
            d = _bfs_distances(room, entry).get(enemy)
            if d is None:
                continue
            if d < MIN_ENEMY_DIST_FROM_ENTRY:
                too_close_entry = entry
                too_close_dist = d
                break
        if too_close_entry is not None:
            issues.append(
                f"enemy {enemy} too close to entry {too_close_entry} "
                f"(dist={too_close_dist}, min={MIN_ENEMY_DIST_FROM_ENTRY})"
            )

    # 4) Safe zone Chebyshev 3x3 autour de chaque entrée — pas d'ennemi
    enemy_set = set(room.enemy_spawns)
    for entry in entries:
        ex, ey = entry
        for dx in range(-SAFE_RADIUS_AROUND_ENTRY, SAFE_RADIUS_AROUND_ENTRY + 1):
            for dy in range(-SAFE_RADIUS_AROUND_ENTRY, SAFE_RADIUS_AROUND_ENTRY + 1):
                cell = (ex + dx, ey + dy)
                if cell in enemy_set:
                    issues.append(
                        f"enemy {cell} inside safe radius {SAFE_RADIUS_AROUND_ENTRY} "
                        f"of entry {entry}"
                    )

    # 5) Choke unique : cut vertex sur le seul chemin entre 2 exits.
    # On exclut :
    #   - les entry points eux-memes
    #   - les "thresholds" : cases adjacentes a une entry (necessairement
    #     chokes car carve_exits cree une porte 1-cell de large -> le seuil
    #     interieur est toujours un cut vertex par construction)
    #   - les sockets (passages legitimes single-point)
    if len(entries) >= 2:
        reachable = set(_bfs_distances(room, entries[0]).keys())
        thresholds: set[tuple[int, int]] = set()
        for ex, ey in entries:
            for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                thresholds.add((ex + dx, ey + dy))
        skip = set(entries) | thresholds | set(room.stair_sockets) | set(room.item_spawns)
        for cell in reachable:
            if cell in skip:
                continue
            if not _connected_without(room, cell, entries):
                issues.append(
                    f"choke point at {cell} -- removing it disconnects exits "
                    f"(consider widening corridor)"
                )

    # 6) Item spawns aussi loin que raisonnable de l'entrée (warning, pas hard fail)
    # Skipped pour l'instant : trop subjectif sans semantic_role (MKD-6).

    # 7) MKD-6 : coherence semantic_role <-> structure
    sem = room.semantic_role
    if sem == SEM_ENTRY and room.enemy_spawns:
        issues.append(
            f"semantic ENTRY but has {len(room.enemy_spawns)} enemy_spawn(s) "
            f"(ENTRY rooms must be safe)"
        )
    if sem == SEM_SAFE and room.enemy_spawns:
        issues.append(
            f"semantic SAFE but has {len(room.enemy_spawns)} enemy_spawn(s) "
            f"(SAFE rooms = 0 enemy)"
        )
    if sem == SEM_STAIR and not room.stair_sockets:
        issues.append(
            f"semantic STAIR but no stair_sockets defined "
            f"(STAIR rooms must have >=1 socket)"
        )
    if sem == SEM_TREASURE and not room.item_spawns and not room.stair_sockets:
        issues.append(
            f"semantic TREASURE but no item_spawns nor stair_sockets "
            f"(TREASURE rooms must offer a reward)"
        )

    return issues


def audit_all_rooms(rooms: list[Room], *, verbose: bool = False) -> dict[str, list[str]]:
    """Run audit on every room. Returns name -> list of issues."""
    report: dict[str, list[str]] = {}
    for room in rooms:
        issues = audit_room(room)
        if issues:
            report[room.name] = issues
        if verbose:
            status = "OK" if not issues else f"{len(issues)} issue(s)"
            print(f"  [{status:>10}] {room.name}")
            for it in issues:
                print(f"             -> {it}")
    return report


def build_rooms() -> list[Room]:
    rooms: list[Room] = []

    rooms.extend(
        rotate_n(
            build_leaf_stair_n(),
            4,
            ["leaf_stair_n", "leaf_stair_e", "leaf_stair_s", "leaf_stair_w"],
        )
    )
    rooms.extend(
        rotate_n(
            build_leaf_void_n(),
            4,
            ["leaf_void_n", "leaf_void_e", "leaf_void_s", "leaf_void_w"],
        )
    )
    rooms.extend(
        rotate_n(
            build_leaf_pillared_n(),
            4,
            ["leaf_pillared_n", "leaf_pillared_e", "leaf_pillared_s", "leaf_pillared_w"],
        )
    )
    rooms.extend(
        rotate_n(
            build_leaf_river_n(),
            4,
            ["leaf_river_n", "leaf_river_e", "leaf_river_s", "leaf_river_w"],
        )
    )
    rooms.extend(
        rotate_n(
            build_corridor_ns(),
            2,
            ["corridor_ns", "corridor_ew"],
        )
    )
    rooms.extend(
        rotate_n(
            build_corridor_pillars_ns(),
            2,
            ["corridor_pillars_ns", "corridor_pillars_ew"],
        )
    )
    rooms.extend(
        rotate_n(
            build_corner_ne(),
            4,
            ["corner_ne", "corner_es", "corner_sw", "corner_wn"],
        )
    )
    rooms.extend(
        rotate_n(
            build_corner_plaza_ne(),
            4,
            ["corner_plaza_ne", "corner_plaza_es", "corner_plaza_sw", "corner_plaza_wn"],
        )
    )
    rooms.extend(
        rotate_n(
            build_tee_nes(),
            4,
            ["tee_nes", "tee_esw", "tee_swn", "tee_wne"],
        )
    )
    rooms.extend(
        rotate_n(
            build_tee_void_nes(),
            4,
            ["tee_void_nes", "tee_void_esw", "tee_void_swn", "tee_void_wne"],
        )
    )
    rooms.append(build_cross_nesw())
    # MKD-7 : rooms uniques (pas de rotation, design fixe)
    rooms.append(build_entrance_unique())

    # MKD-3-A : normalise les rooms non passees par rotate_n (cross_nesw +
    # entrance_unique). Idempotent : si deja normalise, decor_anchors reste
    # inchange et le scan ne trouve rien.
    for room in rooms:
        if not room.decor_anchors:
            normalize_decor_anchors(room)

    for room in rooms:
        validate_room(room)
    return rooms


def c_join_flags(flags: list[str]) -> str:
    if not flags:
        return "0u"
    return " | ".join(flags)


def c_join_cells(room: Room) -> str:
    lines: list[str] = []
    for y, row in enumerate(room.cells):
        enum_cells = [CELL_TO_ENUM[cell] for cell in row]
        line = "    " + ", ".join(enum_cells)
        if y != room.h - 1:
            line += ","
        lines.append(line)
    return "\n".join(lines)


def c_join_points(points: list[tuple[int, int]]) -> str:
    if not points:
        return ""
    parts = [f"    {{ {x}u, {y}u }}" for x, y in points]
    text = ",\n".join(parts)
    return text + "\n"


def ascii_comment(room: Room) -> str:
    lines = [f"/* {room.name} ({room.w}x{room.h}, exits={room.mask_name()})"]
    for row in room.cells:
        lines.append(" * " + "".join(row))
    lines.append(" */")
    return "\n".join(lines)


def emit_header(room_count: int) -> str:
    return f"""/* Generated by gen_static_room_bank.py - do not edit */

#ifndef STATIC_ROOM_BANK_H
#define STATIC_ROOM_BANK_H

#include "ngpc_types.h"

#define STATIC_ROOM_BANK_COUNT {room_count}u
#define STATIC_ROOM_BANK_MIN_W {MIN_W}u
#define STATIC_ROOM_BANK_MIN_H {MIN_H}u
#define STATIC_ROOM_BANK_MAX_W {MAX_W}u
#define STATIC_ROOM_BANK_MAX_H {MAX_H}u
#define STATIC_ROOM_BANK_MAX_MASK_VARIANTS {MAX_VARIANTS_PER_MASK}u
#define STATIC_ROOM_BANK_INVALID_INDEX {INVALID_ROOM_INDEX}u
#define STATIC_ROOM_MAX_DECOR_ANCHORS {MAX_DECOR_ANCHORS_PER_ROOM}u  /* MKD-3-A */

#define STATIC_ROOM_EXIT_N 0x01u
#define STATIC_ROOM_EXIT_E 0x02u
#define STATIC_ROOM_EXIT_S 0x04u
#define STATIC_ROOM_EXIT_W 0x08u

#define STATIC_ROOM_ROLE_NODE 0u
#define STATIC_ROOM_ROLE_LEAF 1u

#define STATIC_ROOM_FEATURE_INNER         0x01u
#define STATIC_ROOM_FEATURE_VOID          0x02u
#define STATIC_ROOM_FEATURE_DECOR         0x04u
#define STATIC_ROOM_FEATURE_STAIR_SOCKET  0x08u
#define STATIC_ROOM_FEATURE_ENEMY_SPAWNS  0x10u
#define STATIC_ROOM_FEATURE_ITEM_SPAWNS   0x20u
#define STATIC_ROOM_FEATURE_ENTRY_UNIQUE  0x40u  /* MKD-7 : prefer for slot 0 */

/* MKD-6 : roles semantiques (drive furnishing MKD-3). Stables, ne pas
 * reordonner (le cluster_generate et le furnishing les hashent par valeur). */
#define STATIC_ROOM_SEMANTIC_ENTRY         0u
#define STATIC_ROOM_SEMANTIC_TRANSIT       1u
#define STATIC_ROOM_SEMANTIC_COMBAT_LIGHT  2u
#define STATIC_ROOM_SEMANTIC_COMBAT_HEAVY  3u
#define STATIC_ROOM_SEMANTIC_TREASURE      4u
#define STATIC_ROOM_SEMANTIC_SAFE          5u
#define STATIC_ROOM_SEMANTIC_STAIR         6u
#define STATIC_ROOM_SEMANTIC_SECRET        7u

typedef enum {{
    STATIC_ROOM_CELL_FLOOR = 0,
    STATIC_ROOM_CELL_CRACK,
    STATIC_ROOM_CELL_OUTER_WALL,
    STATIC_ROOM_CELL_INNER_WALL,
    STATIC_ROOM_CELL_VOID_DROP,
    STATIC_ROOM_CELL_PILLAR,
    STATIC_ROOM_CELL_DECO_TOTEM,
    STATIC_ROOM_CELL_DECO_VASE
}} StaticRoomCell;

typedef struct {{
    u8 x;
    u8 y;
}} StaticRoomPoint;

typedef struct {{
    const char *name;
    u8 w;
    u8 h;
    u8 exits_mask;
    u8 role;
    u8 feature_flags;
    u8 semantic_role;        /* MKD-6 : STATIC_ROOM_SEMANTIC_* */
    u8 door_col_lo;
    u8 door_col_hi;
    u8 door_row_lo;
    u8 door_row_hi;
    u8 enemy_spawn_count;
    u8 item_spawn_count;
    u8 stair_socket_count;
    u8 decor_anchor_count;   /* MKD-3-A : nombre d'anchors decor (runtime decide) */
    const u8 *cells;
    const StaticRoomPoint *enemy_spawns;
    const StaticRoomPoint *item_spawns;
    const StaticRoomPoint *stair_sockets;
    const StaticRoomPoint *decor_anchors;  /* MKD-3-A */
}} StaticRoomDef;

const StaticRoomDef *static_room_bank_get(u8 room_idx);
u8 static_room_bank_count_for_mask(u8 exits_mask);
u8 static_room_bank_index_for_mask(u8 exits_mask, u8 variant_idx);

#endif /* STATIC_ROOM_BANK_H */
"""


def emit_source(rooms: list[Room]) -> str:
    masks: dict[int, list[int]] = defaultdict(list)
    for idx, room in enumerate(rooms):
        masks[room.exits_mask].append(idx)

    index_rows: list[str] = []
    count_rows: list[str] = []
    for mask in range(16):
        count = len(masks[mask])
        count_rows.append(f"    {count}u")
        row = [f"{INVALID_ROOM_INDEX}u"] * MAX_VARIANTS_PER_MASK
        for idx, room_idx in enumerate(masks[mask][:MAX_VARIANTS_PER_MASK]):
            row[idx] = f"{room_idx}u"
        index_rows.append("    { " + ", ".join(row) + " }")

    chunks: list[str] = [
        "/* Generated by gen_static_room_bank.py - do not edit */",
        "",
        '#include "static_room_bank.h"',
        "",
    ]

    for idx, room in enumerate(rooms):
        chunks.append(ascii_comment(room))
        chunks.append(
            f"static const u8 s_room_{idx:02d}_cells[{room.w * room.h}] = {{\n"
            f"{c_join_cells(room)}\n"
            "};"
        )
        if room.enemy_spawns:
            chunks.append(
                f"static const StaticRoomPoint s_room_{idx:02d}_enemy_spawns[{len(room.enemy_spawns)}] = {{\n"
                f"{c_join_points(room.enemy_spawns)}}};"
            )
        else:
            chunks.append(f"static const StaticRoomPoint * const s_room_{idx:02d}_enemy_spawns = 0;")
        if room.item_spawns:
            chunks.append(
                f"static const StaticRoomPoint s_room_{idx:02d}_item_spawns[{len(room.item_spawns)}] = {{\n"
                f"{c_join_points(room.item_spawns)}}};"
            )
        else:
            chunks.append(f"static const StaticRoomPoint * const s_room_{idx:02d}_item_spawns = 0;")
        if room.stair_sockets:
            chunks.append(
                f"static const StaticRoomPoint s_room_{idx:02d}_stair_sockets[{len(room.stair_sockets)}] = {{\n"
                f"{c_join_points(room.stair_sockets)}}};"
            )
        else:
            chunks.append(f"static const StaticRoomPoint * const s_room_{idx:02d}_stair_sockets = 0;")
        # MKD-3-A : decor anchors
        if room.decor_anchors:
            chunks.append(
                f"static const StaticRoomPoint s_room_{idx:02d}_decor_anchors[{len(room.decor_anchors)}] = {{\n"
                f"{c_join_points(room.decor_anchors)}}};"
            )
        else:
            chunks.append(f"static const StaticRoomPoint * const s_room_{idx:02d}_decor_anchors = 0;")
        chunks.append("")

    chunks.append("static const StaticRoomDef s_room_defs[STATIC_ROOM_BANK_COUNT] = {")
    for idx, room in enumerate(rooms):
        role = "STATIC_ROOM_ROLE_LEAF" if room.role == ROLE_LEAF else "STATIC_ROOM_ROLE_NODE"
        flags = c_join_flags(room.feature_flags())
        col_lo, col_hi = room.door_cols()
        row_lo, row_hi = room.door_rows()
        enemy_ref = f"s_room_{idx:02d}_enemy_spawns" if room.enemy_spawns else "0"
        item_ref = f"s_room_{idx:02d}_item_spawns" if room.item_spawns else "0"
        stair_ref = f"s_room_{idx:02d}_stair_sockets" if room.stair_sockets else "0"
        decor_ref = f"s_room_{idx:02d}_decor_anchors" if room.decor_anchors else "0"
        semantic_enum = SEMANTIC_TO_ENUM.get(room.semantic_role,
                                             "STATIC_ROOM_SEMANTIC_TRANSIT")
        chunks.append(
            "    {\n"
            f'        "{room.name}",\n'
            f"        {room.w}u, {room.h}u,\n"
            f"        0x{room.exits_mask:02X}u,\n"
            f"        {role},\n"
            f"        {flags},\n"
            f"        {semantic_enum},\n"
            f"        {col_lo}u, {col_hi}u,\n"
            f"        {row_lo}u, {row_hi}u,\n"
            f"        {len(room.enemy_spawns)}u,\n"
            f"        {len(room.item_spawns)}u,\n"
            f"        {len(room.stair_sockets)}u,\n"
            f"        {len(room.decor_anchors)}u,\n"
            f"        s_room_{idx:02d}_cells,\n"
            f"        {enemy_ref},\n"
            f"        {item_ref},\n"
            f"        {stair_ref},\n"
            f"        {decor_ref}\n"
            "    }" + ("," if idx != len(rooms) - 1 else "")
        )
    chunks.append("};")
    chunks.append("")
    chunks.append("static const u8 s_room_counts_by_mask[16] = {")
    chunks.append(",\n".join(count_rows))
    chunks.append("};")
    chunks.append("")
    chunks.append(
        f"static const u8 s_room_indices_by_mask[16][{MAX_VARIANTS_PER_MASK}] = {{"
    )
    chunks.append(",\n".join(index_rows))
    chunks.append("};")
    chunks.append("")
    chunks.append("const StaticRoomDef *static_room_bank_get(u8 room_idx)")
    chunks.append("{")
    chunks.append("    if (room_idx >= STATIC_ROOM_BANK_COUNT) { return 0; }")
    chunks.append("    return &s_room_defs[room_idx];")
    chunks.append("}")
    chunks.append("")
    chunks.append("u8 static_room_bank_count_for_mask(u8 exits_mask)")
    chunks.append("{")
    chunks.append("    if (exits_mask >= 16u) { return 0u; }")
    chunks.append("    return s_room_counts_by_mask[exits_mask];")
    chunks.append("}")
    chunks.append("")
    chunks.append("u8 static_room_bank_index_for_mask(u8 exits_mask, u8 variant_idx)")
    chunks.append("{")
    chunks.append("    if (exits_mask >= 16u) { return STATIC_ROOM_BANK_INVALID_INDEX; }")
    chunks.append("    if (variant_idx >= s_room_counts_by_mask[exits_mask]) { return STATIC_ROOM_BANK_INVALID_INDEX; }")
    chunks.append("    return s_room_indices_by_mask[exits_mask][variant_idx];")
    chunks.append("}")
    chunks.append("")

    return "\n".join(chunks)


def main() -> int:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--audit",
        action="store_true",
        help="Run extended BFS validation (MKD-4) and exit non-zero on any issue. "
             "Does NOT write the C bank files in audit-only mode.",
    )
    parser.add_argument(
        "--audit-after-build",
        action="store_true",
        help="Generate bank files AND run extended audit. Build succeeds even on audit issues.",
    )
    args = parser.parse_args()

    project_root = Path(__file__).resolve().parent.parent
    out_h = project_root / "src" / "static_room_bank.h"
    out_c = project_root / "src" / "static_room_bank.c"

    rooms = build_rooms()

    if args.audit:
        print(f"\nMKD-4 audit on {len(rooms)} rooms ...\n")
        report = audit_all_rooms(rooms, verbose=True)
        print()
        if report:
            print(f"AUDIT FAIL: {len(report)} room(s) with issues:")
            for name, issues in report.items():
                print(f"  {name}: {len(issues)} issue(s)")
            return 1
        print(f"AUDIT OK: {len(rooms)} rooms clean")
        return 0

    out_h.write_text(emit_header(len(rooms)), encoding="ascii")
    out_c.write_text(emit_source(rooms), encoding="ascii")

    by_mask: dict[int, list[str]] = defaultdict(list)
    for room in rooms:
        by_mask[room.exits_mask].append(room.name)

    print(f"Generated {len(rooms)} rooms.")
    for mask in sorted(by_mask):
        names = ", ".join(by_mask[mask])
        print(f"  mask 0x{mask:02X}: {names}")
    print(f"Wrote: {out_h}")
    print(f"Wrote: {out_c}")

    if args.audit_after_build:
        print(f"\nMKD-4 audit on {len(rooms)} rooms ...\n")
        report = audit_all_rooms(rooms, verbose=True)
        if report:
            print(f"\nAUDIT WARN: {len(report)} room(s) with issues (build succeeded).")
        else:
            print(f"\nAUDIT OK: {len(rooms)} rooms clean.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
