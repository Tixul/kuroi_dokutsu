/* static_room_loader.c */

#include "static_room_loader.h"
#include "../GraphX/tiles_unit.h"
#include "../GraphX/caisse_tiles.h"
#include "../GraphX/door_sheet_tiles.h"
#include "../GraphX/declencheur_tiles.h"

#include "ngpc_gfx.h"
#include "ngpc_hw.h"

/* STRAT-1 : VRAM tile base ou caisse_tiles[] est charge. 4 tiles consecutives
 * (TL TR BL BR). Slot 244 = libre en mode dungeon (entre hent fin 243 et
 * player debut 256). PAL_CAISSE = slot 4 SCR2 (cf s_pal_scr2). */
#define CAISSE_TILE_BASE  244u
#define PAL_CAISSE_SLOT   4u

/* MKD-lock : VRAM bases.
 *   382..397 = door_sheet_tiles_ns (16 tiles, exits N/S, verticale)
 *   398..413 = door_sheet_tiles_we (16 tiles, exits W/E, rotation 90 CCW)
 *   248..251 = declencheur_tiles  (4 tiles, plate)
 * Plage 382..413 libre entre fin TILES_UNIT (382) et HUD_FONT (416).
 * Plage 248..251 libre entre caisse (244..247) et player_topdown (256+). */
#define DOOR_LOCK_NS_TILE_BASE  382u
#define DOOR_LOCK_WE_TILE_BASE  398u
#define TRIGGER_TILE_BASE       248u
#define PAL_DOOR_LOCK_SLOT        5u   /* SCR1 slot 5 */
#define PAL_TRIGGER_SLOT          6u   /* SCR1 slot 6 */

/* Ticks par frame d'anim. 4 transitions * 3 ticks = 12 ticks total = ~0.2s @60Hz. */
#define LOCK_ANIM_TICKS_PER_FRAME  3u

/* Pointeur vers la RoomDef courante, mis a jour par static_room_load(). */
static const StaticRoomDef *s_current = 0;

/* MKD-3-A : seed cluster, defini dans main.c. Permet a chaque salle de varier
 * son decor a chaque session tout en restant reproductible avec une seed fixe. */
extern u8 g_last_cluster_seed;

/* MKD-3-A : decor anchors actives (run-time, apres roll). Stockes pour la
 * collision (chaque decor place = solide). Cap = STATIC_ROOM_MAX_DECOR_ANCHORS.
 * Aujourd'hui seuls les TOTEMS finissent ici (decor passif non-pushable). */
static u8 s_active_decor_count = 0u;
static u8 s_active_decor_x[STATIC_ROOM_MAX_DECOR_ANCHORS];
static u8 s_active_decor_y[STATIC_ROOM_MAX_DECOR_ANCHORS];

/* STRAT-1 : pushables runtime (vases, caisses). Solides pour le joueur ET
 * les enemies. Liste compactee par static_room_pushable_remove. */
static u8 s_pushable_count = 0u;
static u8 s_pushable_x[STATIC_ROOM_MAX_PUSHABLES];
static u8 s_pushable_y[STATIC_ROOM_MAX_PUSHABLES];
static u8 s_pushable_type[STATIC_ROOM_MAX_PUSHABLES];

/* MKD-5 v3 : mask des portes scellees (= remplacees par un mur car le
 * neighbor cluster est STAIR/NONE -> pas de room voisine). Reset par
 * static_room_load, set par static_room_seal_doors. */
static u8 s_sealed_doors_mask = 0u;

/* MKD-lock : etat runtime de la porte verrouillee de la salle courante. */
static u8 s_lock_dir       = 0u;   /* 0 = aucun lock, else STATIC_ROOM_EXIT_* */
static u8 s_lock_tx        = 0u;
static u8 s_lock_ty        = 0u;
static u8 s_lock_frame     = 0u;   /* 0..3 visible */
static u8 s_lock_target    = 0u;   /* 0..3 desired */
static u8 s_lock_held      = 0u;
static u8 s_lock_anim_timer = 0u;

/* RNG xorshift16 local pour le furnishing. Re-seede par room_load(). */
static u16 s_decor_rng_state = 1u;

static void _decor_rng_seed(u16 seed)
{
    s_decor_rng_state = seed ? seed : 0x1A3Cu;
}

static u8 _decor_rng_u8(void)
{
    u16 x = s_decor_rng_state;
    x ^= (u16)(x << 7);
    x ^= (u16)(x >> 9);
    x ^= (u16)(x << 8);
    s_decor_rng_state = x;
    return (u8)(x >> 8);
}

/* Seuil u8 (0..255) pour le roll "place decor ici ?" selon le role semantique.
 * Valeurs choisies cf. AMELIORATIONS_DUNGEON.md MKD-3-A.
 * Pas de modulo : roll < threshold = passe. */
static u8 _decor_threshold_for_role(u8 sem_role)
{
    if (sem_role == STATIC_ROOM_SEMANTIC_ENTRY)        return 250u;  /* ~98% */
    if (sem_role == STATIC_ROOM_SEMANTIC_SAFE)         return 204u;  /* 80%  */
    if (sem_role == STATIC_ROOM_SEMANTIC_TREASURE)     return 179u;  /* 70%  */
    if (sem_role == STATIC_ROOM_SEMANTIC_STAIR)        return 179u;  /* 70%  */
    if (sem_role == STATIC_ROOM_SEMANTIC_TRANSIT)      return 128u;  /*  50% */
    if (sem_role == STATIC_ROOM_SEMANTIC_COMBAT_LIGHT) return 128u;
    if (sem_role == STATIC_ROOM_SEMANTIC_COMBAT_HEAVY) return 76u;   /* 30%  */
    return 128u;
}

/* =========================================================================
 * Helpers
 * ========================================================================= */

/* Le donjon partage les planes/palettes avec d'autres etats (menus, pause,
 * font custom, etc.). Re-appliquer explicitement les palettes du tileset
 * avant chaque rendu de salle evite de garder un slot stale d'une salle/
 * ecran precedent.
 *
 * IMPORTANT bug-fix : on charge depuis const ROM array + index, EXACTEMENT
 * comme NGP_TILEMAP_LOAD_PALETTES_SCR1 le fait pour salle_01 (qui rend
 * olive correctement). Quand on appelle ngpc_gfx_set_palette() avec 4
 * constantes immediates RGB(...), cc900 transmet mal les arguments
 * mixtes u8/u8/u16/u16/u16/u16 : olive 0x699 sort en bleu/violet sur
 * silicium. Le pattern array+loop est le SEUL connu qui marche pour ce
 * compilateur sur les set_palette enchaines. */
static const u16 NGP_FAR s_pal_scr1[] = {
    /* slot 0 PAL_WALL : matche salle_01_palettes pal 0 */
    0x0000, 0x0000, 0x0699, 0x0577,
    /* slot 1 PAL_FLOOR : matche salle_01_palettes pal 1 */
    0x0000, 0x0999, 0x0666, 0x0000,
    /* slot 2 placeholder (non emis) */
    0x0000, 0x0000, 0x0000, 0x0000,
    /* slot 3 placeholder (non emis) */
    0x0000, 0x0000, 0x0000, 0x0000,
    /* slot 4 placeholder (non emis) */
    0x0000, 0x0000, 0x0000, 0x0000,
    /* slot 5 PAL_DOOR_LOCK (MKD-lock) : porte verrouillee marron/stone */
    0x0000, 0x0000, 0x0588, 0x037A,
    /* slot 6 PAL_TRIGGER (MKD-lock) : declencheur gris */
    0x0000, 0x0000, 0x0555, 0x0878,
};

/* SCR2 : 2 palettes emises via boucle array (slot 0 = font ahchay, slot 1 =
 * PAL_DECO). 2 slots minimum pour echapper au constant-fold cc900 qui
 * retombe dans le bug "4 RGB() immediates -> couleurs corrompues" si on
 * appelle ngpc_gfx_set_palette avec un index litteral seul.
 *
 * IMPORTANT slot 0 : c'est la palette de la font custom ahchay, utilisee
 * par ngpc_text_print/ngpc_text_print_num sur SCR2 (damage popups). Si on
 * la laisse noire, la font devient invisible des qu'on rentre dans le
 * donjon. Le macro ahchay_font_set_palette() ne peut PAS la restaurer car
 * il passe 4 RGB() immediates -> casse par cc900. Seule l'ecriture via
 * array+loop ici la maintient correctement a chaque salle. */
static const u16 NGP_FAR s_pal_scr2[] = {
    /* slot 0 PAL_FONT (transparent, pour popups + font originale) :
     *   C0 = transparent, C1 = blanc (letter), C2/C3 = noir */
    0x0000, 0x0FFF, 0x0000, 0x0000,
    /* slot 1 PAL_DECO (matche tiles_unit.h apres re-export hand-picked) :
     *   C1 = brown (7,4,3)   = 0x347 - corps vase + totem mid
     *   C2 = cream (13,12,11)= 0xBCD - highlights
     *   C3 = dark  (1,1,1)   = 0x111 - outlines */
    0x0000, 0x0347, 0x0BCD, 0x0111,
    /* slot 2 = HUD_BG_PAL_SCR2 (cf src/main.c HUD_BG_PAL_SCR2=2) :
     * gere par hud_load_vram qui ecrit la palette propre au hud_bg.png. On
     * la skip ici pour ne pas l'ecraser. */
    /* slot 3 PAL_FONT_OPAQUE (variante HUD font opaque, MKD-misc) :
     *   C0 = unused, C1 = noir opaque (bg), C2 = blanc (letter), C3 = noir */
    0x0000, 0x0347, 0x0BCD, 0x0111,  /* slot 2 placeholder (skipped at write) */
    0x0000, 0x0000, 0x0FFF, 0x0000,  /* slot 3 opaque HUD font */
    /* slot 4 PAL_CAISSE (STRAT-1) : meme couleurs que hent (marrons).
     *   C0 = transparent, C1 = 0x0135 marron fonce (outline),
     *   C2 = 0x0269 marron clair (corps), C3 = unused. */
    0x0000, 0x0135, 0x0269, 0x0000,
};

static void _apply_room_palettes(void)
{
    u8 i;
    u16 src;

    /* SCR1 : slot 0 (PAL_WALL=0) + slot 1 (PAL_FLOOR=1) +
     * slot 5 (PAL_DOOR_LOCK) + slot 6 (PAL_TRIGGER). Slots 2..4 skip. */
    for (i = 0u; i < 7u; i++) {
        if (i >= 2u && i <= 4u) continue;
        src = (u16)i * 4u;
        ngpc_gfx_set_palette(GFX_SCR1, i,
            s_pal_scr1[src],
            s_pal_scr1[(u16)(src + 1u)],
            s_pal_scr1[(u16)(src + 2u)],
            s_pal_scr1[(u16)(src + 3u)]);
    }

    /* SCR2 : emis slots 0, 1, 3, 4 (skip slot 2 = HUD_BG_PAL_SCR2 geree par
     * hud_load_vram avec sa propre palette du bandeau hud_bg.png) :
     *   0 = font transparente (popups)
     *   1 = PAL_DECO (vase/totem)
     *   2 = (skip) HUD_BG
     *   3 = font opaque HUD (bg noir + letter blanc)
     *   4 = PAL_CAISSE (STRAT-1, marrons hent) */
    for (i = 0u; i < 5u; i++) {
        if (i == 2u) continue;  /* slot 2 = HUD_BG, ne pas toucher */
        src = (u16)i * 4u;
        ngpc_gfx_set_palette(GFX_SCR2, i,
            s_pal_scr2[src],
            s_pal_scr2[(u16)(src + 1u)],
            s_pal_scr2[(u16)(src + 2u)],
            s_pal_scr2[(u16)(src + 3u)]);
    }
}

/* Lit le type de cell a (x, y) ou renvoie OUTER_WALL si hors grille.
 * Utilise pour la detection d'orientation des murs (hors grille = "mur"). */
static u8 _cell_at(s8 x, s8 y)
{
    if (s_current == 0) return STATIC_ROOM_CELL_OUTER_WALL;
    if (x < 0 || y < 0) return STATIC_ROOM_CELL_OUTER_WALL;
    if ((u8)x >= s_current->w || (u8)y >= s_current->h) return STATIC_ROOM_CELL_OUTER_WALL;
    return s_current->cells[(u16)y * s_current->w + (u16)x];
}

/* Retourne 1 si la cell est "interieure" du point de vue d'un mur
 * (cote d'ou le mur est regarde, donc face visible). */
static u8 _is_interior(u8 cell)
{
    switch (cell) {
    case STATIC_ROOM_CELL_FLOOR:
    case STATIC_ROOM_CELL_CRACK:
    case STATIC_ROOM_CELL_VOID_DROP:
    case STATIC_ROOM_CELL_PILLAR:
    case STATIC_ROOM_CELL_DECO_TOTEM:
    case STATIC_ROOM_CELL_DECO_VASE:
        return 1u;
    default:
        return 0u;
    }
}

/* =========================================================================
 * Ecriture d'un metatile complet sur un plane
 * =========================================================================
 * Un metatile = 16x16 px = 2x2 tiles NGPC.
 * base_tile = premier tile NGPC (TL) du metatile dans la VRAM.
 * Flip horizontal/vertical du metatile = reordonnancement TL/TR/BL/BR
 * ET flip hardware applique a chaque tile NGPC.
 */
static void _put_mt(u8 plane, u8 mx, u8 my, u16 base_tile, u8 pal,
                    u8 hflip, u8 vflip)
{
    u8 gx, gy;
    u16 tl, tr, bl, br;

    gx = (u8)(mx * 2u);
    gy = (u8)(my * 2u);

    if (!hflip && !vflip) {
        tl = base_tile;      tr = (u16)(base_tile + 1u);
        bl = (u16)(base_tile + 2u); br = (u16)(base_tile + 3u);
    } else if (hflip && !vflip) {
        tl = (u16)(base_tile + 1u); tr = base_tile;
        bl = (u16)(base_tile + 3u); br = (u16)(base_tile + 2u);
    } else if (!hflip && vflip) {
        tl = (u16)(base_tile + 2u); tr = (u16)(base_tile + 3u);
        bl = base_tile;      br = (u16)(base_tile + 1u);
    } else {
        tl = (u16)(base_tile + 3u); tr = (u16)(base_tile + 2u);
        bl = (u16)(base_tile + 1u); br = base_tile;
    }

    ngpc_gfx_put_tile_ex(plane, gx,            gy,            tl, pal, hflip, vflip);
    ngpc_gfx_put_tile_ex(plane, (u8)(gx + 1u), gy,            tr, pal, hflip, vflip);
    ngpc_gfx_put_tile_ex(plane, gx,            (u8)(gy + 1u), bl, pal, hflip, vflip);
    ngpc_gfx_put_tile_ex(plane, (u8)(gx + 1u), (u8)(gy + 1u), br, pal, hflip, vflip);
}

/* =========================================================================
 * Choix tile + flip pour chaque type de cell
 * ========================================================================= */

/* OUTER wall : detection PAR POSITION du cell dans le rectangle room.
 * Les murs exterieurs forment toujours le perimetre, donc c'est fiable.
 * Coins detectes par (mx in {0, w-1}) AND (my in {0, h-1}).
 */
static void _draw_outer_wall(u8 mx, u8 my)
{
    u8 w;
    u8 h;
    u16 tile;
    u8 hflip;
    u8 vflip;
    u8 is_left;
    u8 is_right;
    u8 is_top;
    u8 is_bot;

    w = s_current->w;
    h = s_current->h;
    is_left  = (mx == 0u);
    is_right = (mx == (u8)(w - 1u));
    is_top   = (my == 0u);
    is_bot   = (my == (u8)(h - 1u));

    tile  = TILE_U_WALL_OUTER_N;
    hflip = 0u;
    vflip = 0u;

    /* Coins. PNG wall_outer_nw / wall_outer_ne sont peints du cote
     * oppose (meme issue que wall_outer_w) -> on swap NW <-> NE entre
     * les positions L et R. */
    if (is_top && is_left) {
        tile = TILE_U_WALL_OUTER_NE; hflip = 0u; vflip = 0u;
    } else if (is_top && is_right) {
        tile = TILE_U_WALL_OUTER_NW; hflip = 0u; vflip = 0u;
    } else if (is_bot && is_left) {
        tile = TILE_U_WALL_OUTER_NE; hflip = 0u; vflip = 1u;
    } else if (is_bot && is_right) {
        tile = TILE_U_WALL_OUTER_NW; hflip = 0u; vflip = 1u;
    }
    /* Bordures. */
    else if (is_top) {
        tile = TILE_U_WALL_OUTER_N; hflip = 0u; vflip = 0u;
    } else if (is_bot) {
        tile = TILE_U_WALL_OUTER_N; hflip = 0u; vflip = 1u;
    } else if (is_left) {
        /* PNG wall_outer_w pixels-mur sont peints sur le bord DROIT du
         * metatile (sens "vu depuis l'interieur regardant vers l'ouest"
         * = wall pixels a l'est). Pour un mur ouest a l'ecran (col 0),
         * il faut hflipper pour amener les pixels du wall sur le bord
         * gauche du metatile. */
        tile = TILE_U_WALL_OUTER_W; hflip = 1u; vflip = 0u;
    } else if (is_right) {
        /* Mur est : tile native (pixels mur deja sur le bord droit). */
        tile = TILE_U_WALL_OUTER_W; hflip = 0u; vflip = 0u;
    }
    /* Outer wall qui n'est pas sur le perimetre : cas rare (peninsule),
     * fallback tile plat. */

    _put_mt((u8)GFX_SCR1, mx, my, tile, PAL_WALL, hflip, vflip);
}

/* INNER wall : deduit l'orientation du voisinage (FLOOR/CRACK adjacent). */
static void _draw_inner_wall(u8 mx, u8 my)
{
    u8 n, s, e, w;
    u16 tile;
    u8 hflip;
    u8 vflip;

    n = _is_interior(_cell_at((s8)mx, (s8)(my - 1)));
    s = _is_interior(_cell_at((s8)mx, (s8)(my + 1)));
    e = _is_interior(_cell_at((s8)(mx + 1), (s8)my));
    w = _is_interior(_cell_at((s8)(mx - 1), (s8)my));

    tile  = TILE_U_WALL_INNER_N;
    hflip = 0u;
    vflip = 0u;

    /* Coins (2 voisins interieurs adjacents). */
    if (s && e && !n && !w) {
        tile = TILE_U_WALL_INNER_NW; hflip = 0u; vflip = 0u;
    } else if (s && w && !n && !e) {
        tile = TILE_U_WALL_INNER_NW; hflip = 1u; vflip = 0u;
    } else if (n && e && !s && !w) {
        tile = TILE_U_WALL_INNER_NW; hflip = 0u; vflip = 1u;
    } else if (n && w && !s && !e) {
        tile = TILE_U_WALL_INNER_NW; hflip = 1u; vflip = 1u;
    }
    /* Murs droits. */
    else if (s && !n) {
        tile = TILE_U_WALL_INNER_N; hflip = 0u; vflip = 0u;
    } else if (n && !s) {
        tile = TILE_U_WALL_INNER_N; hflip = 0u; vflip = 1u;
    } else if (e && !w) {
        tile = TILE_U_WALL_INNER_W; hflip = 0u; vflip = 0u;
    } else if (w && !e) {
        tile = TILE_U_WALL_INNER_W; hflip = 1u; vflip = 0u;
    }

    _put_mt((u8)GFX_SCR1, mx, my, tile, PAL_WALL, hflip, vflip);
}

/* VOID_DROP : VOID_FILL au centre, VOID_EDGE_N/W aux bords du trou. */
static void _draw_void(u8 mx, u8 my)
{
    u8 n_wall, s_wall, e_wall, w_wall;
    u16 tile;
    u8 hflip, vflip;

    /* Bord si le voisin n'est PAS un VOID_DROP (donc sol). */
    n_wall = (_cell_at((s8)mx, (s8)(my - 1)) != STATIC_ROOM_CELL_VOID_DROP);
    s_wall = (_cell_at((s8)mx, (s8)(my + 1)) != STATIC_ROOM_CELL_VOID_DROP);
    e_wall = (_cell_at((s8)(mx + 1), (s8)my) != STATIC_ROOM_CELL_VOID_DROP);
    w_wall = (_cell_at((s8)(mx - 1), (s8)my) != STATIC_ROOM_CELL_VOID_DROP);

    tile  = TILE_U_VOID_FILL;
    hflip = 0u;
    vflip = 0u;

    if (n_wall && !s_wall && !e_wall && !w_wall) {
        tile = TILE_U_VOID_EDGE_N; vflip = 0u;
    } else if (s_wall && !n_wall && !e_wall && !w_wall) {
        tile = TILE_U_VOID_EDGE_N; vflip = 1u;
    } else if (w_wall && !n_wall && !s_wall && !e_wall) {
        tile = TILE_U_VOID_EDGE_W; hflip = 0u;
    } else if (e_wall && !n_wall && !s_wall && !w_wall) {
        tile = TILE_U_VOID_EDGE_W; hflip = 1u;
    }
    /* Coins et autres cas : on laisse VOID_FILL pour l'instant. */

    _put_mt((u8)GFX_SCR1, mx, my, tile, PAL_FLOOR, hflip, vflip);
}

/* =========================================================================
 * Chargement VRAM (appel unique)
 * ========================================================================= */

void static_room_loader_init_vram(void)
{
    /* Charge les 72 tiles NGPC de tileset_unit a partir de TILE_U_BASE. */
    ngpc_gfx_load_tiles_at(TILES_UNIT, TILES_UNIT_COUNT, TILE_U_BASE);

    /* STRAT-1 : 4 tiles caisse a CAISSE_TILE_BASE (244..247). Count = nb u16
     * (4 tiles x 8 u16 = 32). Palette PAL_CAISSE slot 4 ecrite par
     * _apply_room_palettes ci-dessous. */
    ngpc_gfx_load_tiles_at(caisse_tiles, 32u, CAISSE_TILE_BASE);

    /* MKD-lock : 16 tiles porte verticale N/S a DOOR_LOCK_NS_TILE_BASE
     * + 16 tiles porte rotee 90 CCW a DOOR_LOCK_WE_TILE_BASE (pour W/E)
     * + 4 tiles declencheur a TRIGGER_TILE_BASE. Counts = nb u16. */
    ngpc_gfx_load_tiles_at(door_sheet_tiles_ns, 128u, DOOR_LOCK_NS_TILE_BASE);
    ngpc_gfx_load_tiles_at(door_sheet_tiles_we, 128u, DOOR_LOCK_WE_TILE_BASE);
    ngpc_gfx_load_tiles_at(declencheur_tiles, 32u, TRIGGER_TILE_BASE);

    _apply_room_palettes();
}

/* =========================================================================
 * Rendu d'une salle
 * ========================================================================= */

/* Rend la geometrie deterministe d'une salle (palettes + sol/murs + portes
 * + escalier) SANS toucher au mobilier random (pushables / active_decor).
 * Reset le mask de portes scellees (le caller doit re-appliquer seal_doors
 * apres si besoin). */
static void _render_room_geometry(const StaticRoomDef *def)
{
    u8 mx, my;
    u8 cell;

    s_sealed_doors_mask = 0u;  /* MKD-5 v3 : reset, sera set par seal_doors */

    _apply_room_palettes();

    ngpc_gfx_clear(GFX_SCR1);
    ngpc_gfx_clear(GFX_SCR2);

    for (my = 0u; my < def->h; my++) {
        for (mx = 0u; mx < def->w; mx++) {
            cell = def->cells[(u16)my * def->w + mx];

            switch (cell) {
            case STATIC_ROOM_CELL_FLOOR:
                _put_mt((u8)GFX_SCR1, mx, my, TILE_U_FLOOR_1, PAL_FLOOR, 0u, 0u);
                break;

            case STATIC_ROOM_CELL_CRACK:
                _put_mt((u8)GFX_SCR1, mx, my, TILE_U_FLOOR_2, PAL_FLOOR, 0u, 0u);
                break;

            case STATIC_ROOM_CELL_OUTER_WALL:
                _draw_outer_wall(mx, my);
                break;

            case STATIC_ROOM_CELL_INNER_WALL:
                _draw_inner_wall(mx, my);
                break;

            case STATIC_ROOM_CELL_VOID_DROP:
                _draw_void(mx, my);
                break;

            case STATIC_ROOM_CELL_PILLAR:
                /* Sol dessous + pilier dessus. Pas de transparence NGPC sur
                 * un metatile bg, donc on met juste le pilier. */
                _put_mt((u8)GFX_SCR1, mx, my, TILE_U_PILLAR, PAL_WALL, 0u, 0u);
                break;

            case STATIC_ROOM_CELL_DECO_TOTEM:
                /* Sol sur SCR1, totem overlay sur SCR2. */
                _put_mt((u8)GFX_SCR1, mx, my, TILE_U_FLOOR_1, PAL_FLOOR, 0u, 0u);
                _put_mt((u8)GFX_SCR2, mx, my, TILE_U_DECO_TOTEM, PAL_DECO, 0u, 0u);
                break;

            case STATIC_ROOM_CELL_DECO_VASE:
                _put_mt((u8)GFX_SCR1, mx, my, TILE_U_FLOOR_1, PAL_FLOOR, 0u, 0u);
                _put_mt((u8)GFX_SCR2, mx, my, TILE_U_DECO_VASE, PAL_DECO, 0u, 0u);
                break;

            default:
                _put_mt((u8)GFX_SCR1, mx, my, TILE_U_FLOOR_1, PAL_FLOOR, 0u, 0u);
                break;
            }
        }
    }

    /* Overlay porte : 1 cell par exit centree (16x16 = largeur joueur).
     * Le bank carve une seule cell FLOOR a door_col_lo (== door_col_hi),
     * on overlay une seule tile DOOR au meme endroit. */
    if (def->exits_mask & STATIC_ROOM_EXIT_N) {
        _put_mt((u8)GFX_SCR1, def->door_col_lo, 0u,
            TILE_U_DOOR_N, PAL_WALL, 0u, 0u);
    }
    if (def->exits_mask & STATIC_ROOM_EXIT_S) {
        _put_mt((u8)GFX_SCR1, def->door_col_lo, (u8)(def->h - 1u),
            TILE_U_DOOR_N, PAL_WALL, 0u, 1u);
    }
    if (def->exits_mask & STATIC_ROOM_EXIT_W) {
        /* Meme issue que wall_outer_w : pixels du bord oppose dans le
         * PNG -> hflip pour amener cote ouest a l'ecran. */
        _put_mt((u8)GFX_SCR1, 0u, def->door_row_lo,
            TILE_U_DOOR_W, PAL_WALL, 1u, 0u);
    }
    if (def->exits_mask & STATIC_ROOM_EXIT_E) {
        _put_mt((u8)GFX_SCR1, (u8)(def->w - 1u), def->door_row_lo,
            TILE_U_DOOR_W, PAL_WALL, 0u, 0u);
    }

    /* Overlay escalier : socket stair_sockets[0] si present. */
    if (def->stair_socket_count > 0u && def->stair_sockets != 0) {
        _put_mt((u8)GFX_SCR1, def->stair_sockets[0].x, def->stair_sockets[0].y,
            TILE_U_STAIR, PAL_WALL, 0u, 0u);
    }
}

/* MKD-3-A + STRAT-1 : pass furnishing.
 * Pour chaque anchor, roll RNG seede. Si roll_place < threshold(role),
 * 3-way sur roll_type :
 *   roll_type < 128  -> TOTEM   (decor passif, dans s_active_decor)
 *   roll_type < 204  -> VASE    (pushable, dans s_pushable) ~30%
 *   roll_type >= 204 -> CAISSE  (pushable, dans s_pushable) ~20%
 * Tile place sur SCR2 dans tous les cas avec la palette adaptee.
 *
 * En plus : si la room a une stair socket, roll ~25% pour cacher la
 * stair sous une caisse (le joueur doit la pousser avant de descendre).
 *
 * V2 envisagee : caisses scriptees via room def (placement explicite
 * pour puzzles dedies, en plus du placement random ici). */
static void _furnish_room(const StaticRoomDef *def, u8 room_idx)
{
    s_active_decor_count = 0u;
    s_pushable_count = 0u;
    {
        u16 seed;
        seed = (u16)g_last_cluster_seed ^ (u16)((u16)room_idx * 31u);
        _decor_rng_seed(seed);
    }
    if (def->decor_anchor_count > 0u && def->decor_anchors != 0) {
        u8 ai;
        u8 threshold;
        u8 roll_place;
        u8 roll_type;
        u8 ax, ay;
        u16 tile;
        u8 kind;    /* 0=TOTEM, 1=VASE, 2=CAISSE */
        u8 pal;

        threshold = _decor_threshold_for_role(def->semantic_role);

        for (ai = 0u; ai < def->decor_anchor_count; ai++) {
            roll_place = _decor_rng_u8();
            roll_type  = _decor_rng_u8();
            if (roll_place >= threshold) continue;

            ax = def->decor_anchors[ai].x;
            ay = def->decor_anchors[ai].y;

            if (roll_type < 128u) {
                kind = 0u;  /* TOTEM */
                tile = (u16)TILE_U_DECO_TOTEM;
                pal  = (u8)PAL_DECO;
            } else if (roll_type < 204u) {
                kind = 1u;  /* VASE */
                tile = (u16)TILE_U_DECO_VASE;
                pal  = (u8)PAL_DECO;
            } else {
                kind = 2u;  /* CAISSE */
                tile = (u16)CAISSE_TILE_BASE;
                pal  = (u8)PAL_CAISSE_SLOT;
            }

            /* Sol sur SCR1 (deja place via cells[]), decor/pushable overlay sur SCR2. */
            _put_mt((u8)GFX_SCR2, ax, ay, tile, pal, 0u, 0u);

            if (kind == 0u) {
                if (s_active_decor_count < STATIC_ROOM_MAX_DECOR_ANCHORS) {
                    s_active_decor_x[s_active_decor_count] = ax;
                    s_active_decor_y[s_active_decor_count] = ay;
                    s_active_decor_count++;
                }
            } else {
                if (s_pushable_count < STATIC_ROOM_MAX_PUSHABLES) {
                    s_pushable_x[s_pushable_count] = ax;
                    s_pushable_y[s_pushable_count] = ay;
                    s_pushable_type[s_pushable_count] =
                        (u8)(kind == 1u ? PUSHABLE_TYPE_VASE : PUSHABLE_TYPE_CAISSE);
                    s_pushable_count++;
                }
            }
        }
    }

    /* Stair-caisse : ~25% de chance qu'une caisse cache la stair_socket[0].
     * Le joueur doit alors pousser la caisse pour reveler l'escalier. */
    if (def->stair_socket_count > 0u && def->stair_sockets != 0 &&
        s_pushable_count < STATIC_ROOM_MAX_PUSHABLES) {
        u8 roll_stair = _decor_rng_u8();
        if (roll_stair < 64u) {
            u8 sx = def->stair_sockets[0].x;
            u8 sy = def->stair_sockets[0].y;
            u8 ok = 1u;
            u8 i;

            for (i = 0u; i < s_active_decor_count; i++) {
                if (s_active_decor_x[i] == sx && s_active_decor_y[i] == sy) {
                    ok = 0u; break;
                }
            }
            if (ok) {
                for (i = 0u; i < s_pushable_count; i++) {
                    if (s_pushable_x[i] == sx && s_pushable_y[i] == sy) {
                        ok = 0u; break;
                    }
                }
            }
            if (ok) {
                _put_mt((u8)GFX_SCR2, sx, sy,
                    (u16)CAISSE_TILE_BASE, (u8)PAL_CAISSE_SLOT, 0u, 0u);
                s_pushable_x[s_pushable_count] = sx;
                s_pushable_y[s_pushable_count] = sy;
                s_pushable_type[s_pushable_count] = PUSHABLE_TYPE_CAISSE;
                s_pushable_count++;
            }
        }
    }
}

void static_room_load(u8 room_idx)
{
    const StaticRoomDef *def;

    def = static_room_bank_get(room_idx);
    if (def == 0) return;
    s_current = def;

    _render_room_geometry(def);
    _furnish_room(def, room_idx);
}

/* MKD-misc : redessine la salle courante a partir de l'etat runtime
 * conserve (s_current + s_pushable_* + s_active_decor_*) SANS regenerer
 * le mobilier random. Usage : retour depuis STATE_PAUSE / STATE_MINIMAP /
 * STATE_OPTIONS, ou la VRAM a ete ecrasee par le menu mais ou l'etat
 * gameplay (positions de caisses/vases poussees, items consommes) doit
 * etre preserve.
 *
 * Reset s_sealed_doors_mask comme le fait static_room_load ; le caller
 * doit re-appliquer static_room_seal_doors apres si besoin. */
void static_room_redraw_current(void)
{
    u8 i;

    if (s_current == 0) return;

    _render_room_geometry(s_current);

    /* Redessine les totems (active_decor) a leur position conservee. */
    for (i = 0u; i < s_active_decor_count; i++) {
        _put_mt((u8)GFX_SCR2, s_active_decor_x[i], s_active_decor_y[i],
            (u16)TILE_U_DECO_TOTEM, (u8)PAL_DECO, 0u, 0u);
    }

    /* Redessine les pushables (vases / caisses) a leur position courante
     * — peut differer du placement initial si le joueur a pousse. */
    for (i = 0u; i < s_pushable_count; i++) {
        u8 type = s_pushable_type[i];
        u16 tile = (type == PUSHABLE_TYPE_CAISSE)
            ? (u16)CAISSE_TILE_BASE : (u16)TILE_U_DECO_VASE;
        u8 pal = (type == PUSHABLE_TYPE_CAISSE)
            ? (u8)PAL_CAISSE_SLOT : (u8)PAL_DECO;
        _put_mt((u8)GFX_SCR2, s_pushable_x[i], s_pushable_y[i],
            tile, pal, 0u, 0u);
    }

    /* MKD-lock : redessine la porte verrouillee + le declencheur si actif. */
    static_room_lock_redraw();
}

/* =========================================================================
 * Collision runtime
 * ========================================================================= */

u8 static_room_collision_at(s8 x, s8 y)
{
    u8 cell;
    u8 i;

    if (s_current == 0) return STATIC_ROOM_COL_SOLID;
    if (x < 0 || y < 0) return STATIC_ROOM_COL_SOLID;
    if ((u8)x >= s_current->w || (u8)y >= s_current->h) return STATIC_ROOM_COL_SOLID;

    /* MKD-5 v3 : portes scellees -> SOLID. Une porte est scellee si
     * static_room_seal_doors a marque sa direction dans s_sealed_doors_mask
     * (cas : cluster neighbor STAIR/NONE -> porte traitee comme mur). */
    if (s_sealed_doors_mask != 0u) {
        if ((s_sealed_doors_mask & STATIC_ROOM_EXIT_N) &&
            (u8)y == 0u &&
            ((u8)x == s_current->door_col_lo || (u8)x == s_current->door_col_hi)) {
            return STATIC_ROOM_COL_SOLID;
        }
        if ((s_sealed_doors_mask & STATIC_ROOM_EXIT_S) &&
            (u8)y == (u8)(s_current->h - 1u) &&
            ((u8)x == s_current->door_col_lo || (u8)x == s_current->door_col_hi)) {
            return STATIC_ROOM_COL_SOLID;
        }
        if ((s_sealed_doors_mask & STATIC_ROOM_EXIT_W) &&
            (u8)x == 0u &&
            ((u8)y == s_current->door_row_lo || (u8)y == s_current->door_row_hi)) {
            return STATIC_ROOM_COL_SOLID;
        }
        if ((s_sealed_doors_mask & STATIC_ROOM_EXIT_E) &&
            (u8)x == (u8)(s_current->w - 1u) &&
            ((u8)y == s_current->door_row_lo || (u8)y == s_current->door_row_hi)) {
            return STATIC_ROOM_COL_SOLID;
        }
    }

    /* MKD-lock : porte verrouillee pas fully open -> SOLID sur sa cell.
     * Position de la porte par direction :
     *   N -> (door_col_lo, 0)
     *   S -> (door_col_lo, h-1)
     *   W -> (0, door_row_lo)
     *   E -> (w-1, door_row_lo) */
    if (s_lock_dir != 0u && s_lock_frame < LOCK_DOOR_FRAME_OPEN) {
        if (s_lock_dir == STATIC_ROOM_EXIT_N &&
            (u8)y == 0u && (u8)x == s_current->door_col_lo) {
            return STATIC_ROOM_COL_SOLID;
        }
        if (s_lock_dir == STATIC_ROOM_EXIT_S &&
            (u8)y == (u8)(s_current->h - 1u) && (u8)x == s_current->door_col_lo) {
            return STATIC_ROOM_COL_SOLID;
        }
        if (s_lock_dir == STATIC_ROOM_EXIT_W &&
            (u8)x == 0u && (u8)y == s_current->door_row_lo) {
            return STATIC_ROOM_COL_SOLID;
        }
        if (s_lock_dir == STATIC_ROOM_EXIT_E &&
            (u8)x == (u8)(s_current->w - 1u) && (u8)y == s_current->door_row_lo) {
            return STATIC_ROOM_COL_SOLID;
        }
    }

    cell = s_current->cells[(u16)y * s_current->w + (u16)x];

    switch (cell) {
    case STATIC_ROOM_CELL_FLOOR:
    case STATIC_ROOM_CELL_CRACK:
        /* Ordre des checks : decor/pushable D'ABORD, puis stair.
         * Raison : si une caisse est posee SUR la stair_socket, on veut
         * retourner SOLID (caisse) pour que le joueur soit oblige de la
         * pousser. Si on checkait stair en premier, on retournerait STAIR
         * et dungeon_try_move laisserait le joueur traverser sans pousser. */

        /* MKD-3-A : decor place au runtime = SOLID (bloque le joueur). */
        for (i = 0u; i < s_active_decor_count; i++) {
            if (s_active_decor_x[i] == (u8)x && s_active_decor_y[i] == (u8)y) {
                return STATIC_ROOM_COL_SOLID;
            }
        }
        /* STRAT-1 : pushable place au runtime = SOLID (bloque). Le caller
         * (dungeon_try_move) peut detecter la presence d'un pushable via
         * static_room_pushable_at et tenter le push avant de traiter SOLID
         * comme un mur. */
        for (i = 0u; i < s_pushable_count; i++) {
            if (s_pushable_x[i] == (u8)x && s_pushable_y[i] == (u8)y) {
                return STATIC_ROOM_COL_SOLID;
            }
        }
        /* Socket stair -> STAIR_TRIGGER (utilise par PAD_A handler pour
         * descendre). Cf ordre : decor/pushable plus haut. */
        if (s_current->stair_socket_count > 0u && s_current->stair_sockets != 0) {
            for (i = 0u; i < s_current->stair_socket_count; i++) {
                if (s_current->stair_sockets[i].x == (u8)x &&
                    s_current->stair_sockets[i].y == (u8)y) {
                    return STATIC_ROOM_COL_STAIR;
                }
            }
        }
        return STATIC_ROOM_COL_PASS;

    case STATIC_ROOM_CELL_VOID_DROP:
        return STATIC_ROOM_COL_VOID;

    case STATIC_ROOM_CELL_OUTER_WALL:
    case STATIC_ROOM_CELL_INNER_WALL:
    case STATIC_ROOM_CELL_PILLAR:
    case STATIC_ROOM_CELL_DECO_TOTEM:
    case STATIC_ROOM_CELL_DECO_VASE:
    default:
        return STATIC_ROOM_COL_SOLID;
    }
}

/* =========================================================================
 * MKD-5 v3 : Seal doors (remplace porte par mur quand pas de neighbor)
 * ========================================================================= */
void static_room_seal_doors(u8 seal_mask)
{
    if (s_current == 0) return;
    s_sealed_doors_mask = seal_mask;
    if (seal_mask == 0u) return;

    /* Pour chaque direction scellee, remplace le tile DOOR par le tile WALL
     * correspondant (meme palette PAL_WALL + flip identique au _draw_outer_wall).
     * Position du door : door_col_lo sur top/bottom row, door_row_lo sur W/E col. */
    if (seal_mask & STATIC_ROOM_EXIT_N) {
        /* Top edge mid : TILE_U_WALL_OUTER_N, no flip */
        _put_mt((u8)GFX_SCR1, s_current->door_col_lo, 0u,
            TILE_U_WALL_OUTER_N, PAL_WALL, 0u, 0u);
    }
    if (seal_mask & STATIC_ROOM_EXIT_S) {
        /* Bottom edge mid : TILE_U_WALL_OUTER_N vflip */
        _put_mt((u8)GFX_SCR1, s_current->door_col_lo, (u8)(s_current->h - 1u),
            TILE_U_WALL_OUTER_N, PAL_WALL, 0u, 1u);
    }
    if (seal_mask & STATIC_ROOM_EXIT_W) {
        /* Left edge mid : TILE_U_WALL_OUTER_W hflip (matching _draw_outer_wall is_left) */
        _put_mt((u8)GFX_SCR1, 0u, s_current->door_row_lo,
            TILE_U_WALL_OUTER_W, PAL_WALL, 1u, 0u);
    }
    if (seal_mask & STATIC_ROOM_EXIT_E) {
        /* Right edge mid : TILE_U_WALL_OUTER_W no flip */
        _put_mt((u8)GFX_SCR1, (u8)(s_current->w - 1u), s_current->door_row_lo,
            TILE_U_WALL_OUTER_W, PAL_WALL, 0u, 0u);
    }
}

/* =========================================================================
 * Accesseurs
 * ========================================================================= */

const StaticRoomDef *static_room_current(void)
{
    return s_current;
}

u8 static_room_w(void)
{
    return (s_current != 0) ? s_current->w : 0u;
}

u8 static_room_h(void)
{
    return (s_current != 0) ? s_current->h : 0u;
}

u8 static_room_entry_position(u8 entry_side, u8 *gx, u8 *gy)
{
    if (s_current == 0) return 0u;

    switch (entry_side) {
    case STATIC_ROOM_ENTRY_NORTH:
        if (s_current->exits_mask & STATIC_ROOM_EXIT_N) {
            *gx = s_current->door_col_lo;
            *gy = 1u;                        /* juste a l'interieur sous la porte N */
            return 1u;
        }
        break;
    case STATIC_ROOM_ENTRY_SOUTH:
        if (s_current->exits_mask & STATIC_ROOM_EXIT_S) {
            *gx = s_current->door_col_lo;
            *gy = (u8)(s_current->h - 2u);
            return 1u;
        }
        break;
    case STATIC_ROOM_ENTRY_WEST:
        if (s_current->exits_mask & STATIC_ROOM_EXIT_W) {
            *gx = 1u;
            *gy = s_current->door_row_lo;
            return 1u;
        }
        break;
    case STATIC_ROOM_ENTRY_EAST:
        if (s_current->exits_mask & STATIC_ROOM_EXIT_E) {
            *gx = (u8)(s_current->w - 2u);
            *gy = s_current->door_row_lo;
            return 1u;
        }
        break;
    default:
        break;
    }

    /* Fallback (entry NONE ou direction sans exit) : viser le centre de la
     * salle, mais si la cell centre n'est pas PASS (= void / mur / pushable /
     * decor / stair), chercher la cell PASS la plus proche en distance
     * Manhattan. Sinon le joueur peut pop sur un void ou un pilier interieur
     * (cf bug 2026-05-17 transition cluster). */
    {
        u8 cx = (u8)(s_current->w / 2u);
        u8 cy = (u8)(s_current->h / 2u);
        u8 best_x = cx;
        u8 best_y = cy;
        u8 best_dist = 0xFFu;
        u8 x;
        u8 y;
        s8 ddx;
        s8 ddy;
        u8 d;

        for (y = 1u; y < (u8)(s_current->h - 1u); y++) {
            for (x = 1u; x < (u8)(s_current->w - 1u); x++) {
                if (static_room_collision_at((s8)x, (s8)y) != STATIC_ROOM_COL_PASS)
                    continue;
                ddx = (s8)x - (s8)cx;
                ddy = (s8)y - (s8)cy;
                if (ddx < 0) ddx = (s8)-ddx;
                if (ddy < 0) ddy = (s8)-ddy;
                d = (u8)((u8)ddx + (u8)ddy);
                if (d < best_dist) {
                    best_dist = d;
                    best_x = x;
                    best_y = y;
                    if (d == 0u) goto found;  /* centre lui-meme est PASS */
                }
            }
        }
    found:
        *gx = best_x;
        *gy = best_y;
    }
    return 0u;
}

/* =========================================================================
 * STRAT-1 : Pushables API
 * ========================================================================= */

/* Tile id pour un type pushable. */
static u16 _pushable_tile_for_type(u8 type)
{
    if (type == PUSHABLE_TYPE_VASE)   return (u16)TILE_U_DECO_VASE;
    if (type == PUSHABLE_TYPE_CAISSE) return (u16)CAISSE_TILE_BASE;
    return 0u;
}

/* Slot de palette pour un type pushable.
 *   VASE  -> PAL_DECO (slot 1 SCR2)
 *   CAISSE-> PAL_CAISSE (slot 4 SCR2) */
static u8 _pushable_pal_for_type(u8 type)
{
    if (type == PUSHABLE_TYPE_CAISSE) return (u8)PAL_CAISSE_SLOT;
    return (u8)PAL_DECO;  /* VASE et fallback */
}

/* Efface un metatile sur un plan (ecrit 4 entries 0 = tile 0 + pal 0 + no flip).
 * Tile 0 NGPC BG = transparent par convention. */
static void _clear_mt(u8 plane, u8 mx, u8 my)
{
    u8 gx, gy;
    gx = (u8)(mx * 2u);
    gy = (u8)(my * 2u);
    ngpc_gfx_put_tile_ex(plane, gx,            gy,            0u, 0u, 0u, 0u);
    ngpc_gfx_put_tile_ex(plane, (u8)(gx + 1u), gy,            0u, 0u, 0u, 0u);
    ngpc_gfx_put_tile_ex(plane, gx,            (u8)(gy + 1u), 0u, 0u, 0u, 0u);
    ngpc_gfx_put_tile_ex(plane, (u8)(gx + 1u), (u8)(gy + 1u), 0u, 0u, 0u, 0u);
}

u8 static_room_pushable_count(void)
{
    return s_pushable_count;
}

u8 static_room_pushable_at(u8 x, u8 y)
{
    u8 i;
    for (i = 0u; i < s_pushable_count; i++) {
        if (s_pushable_x[i] == x && s_pushable_y[i] == y) return i;
    }
    return 0xFFu;
}

u8 static_room_pushable_push(u8 from_x, u8 from_y, s8 dx, s8 dy)
{
    u8 idx;
    s8 nx, ny;
    u8 col;
    u16 tile;

    idx = static_room_pushable_at(from_x, from_y);
    if (idx == 0xFFu) return 0u;

    if (s_current == 0) return 2u;

    nx = (s8)((s8)from_x + dx);
    ny = (s8)((s8)from_y + dy);

    /* Bounds */
    if (nx < 0 || ny < 0) return 2u;
    if ((u8)nx >= s_current->w || (u8)ny >= s_current->h) return 2u;

    /* Collision standard : couvre mur, void, stair socket, decor, autre
     * pushable, sealed doors. Seul PASS autorise le push (sauf VOID :
     * cas special, le pushable tombe et est consomme). */
    col = static_room_collision_at(nx, ny);
    if (col == STATIC_ROOM_COL_VOID) {
        /* Pushable tombe dans le vide : efface tile SCR2 + retire des
         * arrays runtime. SCR1 conserve le void deja dessine, le joueur
         * peut ensuite avancer sur la cell d'ou le pushable a ete pousse
         * (geree par le caller via le return 1u standard). */
        static_room_pushable_remove(idx);
        return 1u;
    }
    if (col != STATIC_ROOM_COL_PASS) return 2u;

    /* Bordure d'une porte ouverte : la cell est FLOOR (carve par le bank)
     * donc collision_at retourne PASS. On bloque explicitement pour ne pas
     * coincer un pushable dans la porte (sans sens + permettrait au joueur
     * de passer par la porte ensuite). */
    {
        u8 ux = (u8)nx;
        u8 uy = (u8)ny;
        u8 ex = s_current->exits_mask;
        if ((ex & STATIC_ROOM_EXIT_N) && uy == 0u &&
            (ux == s_current->door_col_lo || ux == s_current->door_col_hi)) {
            return 2u;
        }
        if ((ex & STATIC_ROOM_EXIT_S) && uy == (u8)(s_current->h - 1u) &&
            (ux == s_current->door_col_lo || ux == s_current->door_col_hi)) {
            return 2u;
        }
        if ((ex & STATIC_ROOM_EXIT_W) && ux == 0u &&
            (uy == s_current->door_row_lo || uy == s_current->door_row_hi)) {
            return 2u;
        }
        if ((ex & STATIC_ROOM_EXIT_E) && ux == (u8)(s_current->w - 1u) &&
            (uy == s_current->door_row_lo || uy == s_current->door_row_hi)) {
            return 2u;
        }
    }

    /* Push OK : efface ancien tile + place nouveau + update runtime. */
    {
        u8 type = s_pushable_type[idx];
        tile = _pushable_tile_for_type(type);
        _clear_mt((u8)GFX_SCR2, from_x, from_y);
        _put_mt((u8)GFX_SCR2, (u8)nx, (u8)ny, tile,
            _pushable_pal_for_type(type), 0u, 0u);
    }
    s_pushable_x[idx] = (u8)nx;
    s_pushable_y[idx] = (u8)ny;
    return 1u;
}

void static_room_pushable_remove(u8 idx)
{
    u8 last;

    if (idx >= s_pushable_count) return;

    /* Efface le tile a l'ecran. */
    _clear_mt((u8)GFX_SCR2, s_pushable_x[idx], s_pushable_y[idx]);

    /* Compacte le tableau : swap avec le dernier. */
    last = (u8)(s_pushable_count - 1u);
    if (idx != last) {
        s_pushable_x[idx] = s_pushable_x[last];
        s_pushable_y[idx] = s_pushable_y[last];
        s_pushable_type[idx] = s_pushable_type[last];
    }
    s_pushable_count = last;
}

/* =========================================================================
 * MKD-lock : porte verrouillee + declencheur
 * ========================================================================= */

/* Calcule position metatile + flips de la porte verrouillee selon
 * s_lock_dir. Retourne 1 si lock actif et orientation supportee. */
static u8 _lock_door_cell(u8 *gx, u8 *gy, u8 *hflip, u8 *vflip, u8 *use_we)
{
    if (s_current == 0 || s_lock_dir == 0u) return 0u;
    *hflip = 0u;
    *vflip = 0u;
    *use_we = 0u;
    if (s_lock_dir == STATIC_ROOM_EXIT_N) {
        *gx = s_current->door_col_lo;
        *gy = 0u;
        return 1u;
    }
    if (s_lock_dir == STATIC_ROOM_EXIT_S) {
        *gx = s_current->door_col_lo;
        *gy = (u8)(s_current->h - 1u);
        *vflip = 1u;
        return 1u;
    }
    if (s_lock_dir == STATIC_ROOM_EXIT_W) {
        *gx = 0u;
        *gy = s_current->door_row_lo;
        *use_we = 1u;
        return 1u;
    }
    if (s_lock_dir == STATIC_ROOM_EXIT_E) {
        *gx = (u8)(s_current->w - 1u);
        *gy = s_current->door_row_lo;
        *use_we = 1u;
        *hflip = 1u;
        return 1u;
    }
    return 0u;
}

static void _draw_lock_door(void)
{
    u8 gx, gy, hf, vf, we;
    if (!_lock_door_cell(&gx, &gy, &hf, &vf, &we)) return;
    if (s_lock_frame >= LOCK_DOOR_FRAME_OPEN) {
        /* Fully open : reutilise la porte existante TILE_U_DOOR_N/W
         * avec les memes flips que _render_room_geometry. */
        if (s_lock_dir == STATIC_ROOM_EXIT_N) {
            _put_mt((u8)GFX_SCR1, gx, gy, TILE_U_DOOR_N, PAL_WALL, 0u, 0u);
        } else if (s_lock_dir == STATIC_ROOM_EXIT_S) {
            _put_mt((u8)GFX_SCR1, gx, gy, TILE_U_DOOR_N, PAL_WALL, 0u, 1u);
        } else if (s_lock_dir == STATIC_ROOM_EXIT_W) {
            _put_mt((u8)GFX_SCR1, gx, gy, TILE_U_DOOR_W, PAL_WALL, 1u, 0u);
        } else { /* E */
            _put_mt((u8)GFX_SCR1, gx, gy, TILE_U_DOOR_W, PAL_WALL, 0u, 0u);
        }
    } else {
        u16 base = we ? (u16)DOOR_LOCK_WE_TILE_BASE : (u16)DOOR_LOCK_NS_TILE_BASE;
        base = (u16)(base + (u16)s_lock_frame * 4u);
        _put_mt((u8)GFX_SCR1, gx, gy, base, PAL_DOOR_LOCK_SLOT, hf, vf);
    }
}

static void _draw_trigger(void)
{
    if (s_lock_dir == 0u) return;
    _put_mt((u8)GFX_SCR1, s_lock_tx, s_lock_ty,
        (u16)TRIGGER_TILE_BASE, (u8)PAL_TRIGGER_SLOT, 0u, 0u);
}

void static_room_lock_init(u8 lock_dir, u8 tx, u8 ty, u8 frame, u8 held)
{
    s_lock_dir = lock_dir;
    s_lock_tx = tx;
    s_lock_ty = ty;
    s_lock_frame = frame;
    s_lock_target = held ? (u8)LOCK_DOOR_FRAME_OPEN : (u8)LOCK_DOOR_FRAME_CLOSED;
    s_lock_held = held;
    s_lock_anim_timer = 0u;
    if (lock_dir != 0u) {
        _draw_trigger();
        _draw_lock_door();
    }
}

void static_room_lock_clear(void)
{
    s_lock_dir = 0u;
    s_lock_tx = 0u;
    s_lock_ty = 0u;
    s_lock_frame = 0u;
    s_lock_target = 0u;
    s_lock_held = 0u;
    s_lock_anim_timer = 0u;
}

u8 static_room_lock_dir(void)        { return s_lock_dir; }
u8 static_room_lock_trigger_x(void)  { return s_lock_tx; }
u8 static_room_lock_trigger_y(void)  { return s_lock_ty; }
u8 static_room_lock_frame(void)      { return s_lock_frame; }
u8 static_room_lock_held(void)       { return s_lock_held; }

void static_room_lock_set_held(u8 held)
{
    if (s_lock_dir == 0u) return;
    s_lock_held = held;
    s_lock_target = held ? (u8)LOCK_DOOR_FRAME_OPEN : (u8)LOCK_DOOR_FRAME_CLOSED;
}

void static_room_lock_tick(void)
{
    if (s_lock_dir == 0u) return;
    if (s_lock_frame == s_lock_target) return;
    s_lock_anim_timer++;
    if (s_lock_anim_timer < LOCK_ANIM_TICKS_PER_FRAME) return;
    s_lock_anim_timer = 0u;
    if (s_lock_frame < s_lock_target) {
        s_lock_frame++;
    } else {
        s_lock_frame--;
    }
    _draw_lock_door();
}

u8 static_room_lock_blocks_at(u8 x, u8 y)
{
    u8 gx, gy, hf, vf, we;
    if (!_lock_door_cell(&gx, &gy, &hf, &vf, &we)) return 0u;
    if (s_lock_frame >= LOCK_DOOR_FRAME_OPEN) return 0u;
    return (u8)((x == gx && y == gy) ? 1u : 0u);
}

void static_room_lock_redraw(void)
{
    if (s_lock_dir == 0u) return;
    _draw_trigger();
    _draw_lock_door();
}

/* MKD-lock : helper "anchor libre" pour positionner le trigger. */
static u8 _anchor_is_free(u8 ax, u8 ay)
{
    u8 i;
    for (i = 0u; i < s_active_decor_count; i++) {
        if (s_active_decor_x[i] == ax && s_active_decor_y[i] == ay) return 0u;
    }
    for (i = 0u; i < s_pushable_count; i++) {
        if (s_pushable_x[i] == ax && s_pushable_y[i] == ay) return 0u;
    }
    return 1u;
}

u8 static_room_free_anchor_count(void)
{
    u8 ai, count;
    if (s_current == 0 || s_current->decor_anchors == 0) return 0u;
    count = 0u;
    for (ai = 0u; ai < s_current->decor_anchor_count; ai++) {
        if (_anchor_is_free(s_current->decor_anchors[ai].x,
                            s_current->decor_anchors[ai].y)) {
            count++;
        }
    }
    return count;
}

u8 static_room_get_free_anchor(u8 idx, u8 *out_x, u8 *out_y)
{
    u8 ai, seen;
    if (s_current == 0 || s_current->decor_anchors == 0) return 0u;
    seen = 0u;
    for (ai = 0u; ai < s_current->decor_anchor_count; ai++) {
        u8 ax = s_current->decor_anchors[ai].x;
        u8 ay = s_current->decor_anchors[ai].y;
        if (_anchor_is_free(ax, ay)) {
            if (seen == idx) {
                *out_x = ax;
                *out_y = ay;
                return 1u;
            }
            seen++;
        }
    }
    return 0u;
}

u8 static_room_pushable_type_at(u8 x, u8 y)
{
    u8 idx = static_room_pushable_at(x, y);
    if (idx == 0xFFu) return PUSHABLE_TYPE_NONE;
    return s_pushable_type[idx];
}

void static_room_pushable_add_at(u8 x, u8 y, u8 type)
{
    u16 tile;
    u8 pal;
    if (type == PUSHABLE_TYPE_NONE) return;
    if (s_pushable_count >= STATIC_ROOM_MAX_PUSHABLES) return;
    if (static_room_pushable_at(x, y) != 0xFFu) return;

    s_pushable_x[s_pushable_count] = x;
    s_pushable_y[s_pushable_count] = y;
    s_pushable_type[s_pushable_count] = type;
    s_pushable_count++;

    tile = _pushable_tile_for_type(type);
    pal = _pushable_pal_for_type(type);
    _put_mt((u8)GFX_SCR2, x, y, tile, pal, 0u, 0u);
}
