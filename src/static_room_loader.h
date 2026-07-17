/* static_room_loader.h
 *
 * Charge et rend une salle depuis static_room_bank vers la VRAM :
 *   - Terrain (sol, murs, portes, escalier, vide, pilier) -> SCR1
 *   - Decors (vase, totem) -> SCR2
 *   - Collision runtime lue directement depuis RoomDef.cells[]
 *
 * Les tiles doivent etre chargees une seule fois par session via
 * static_room_loader_init_vram() (typiquement dans dungeon_init).
 */

#ifndef STATIC_ROOM_LOADER_H
#define STATIC_ROOM_LOADER_H

#include "ngpc_types.h"
#include "static_room_bank.h"

/* Valeurs de collision retournees par static_room_collision_at(). */
#define STATIC_ROOM_COL_PASS   0u   /* traversable */
#define STATIC_ROOM_COL_SOLID  1u   /* mur / pilier / deco */
#define STATIC_ROOM_COL_VOID   2u   /* fosse : transition punitive */
#define STATIC_ROOM_COL_STAIR  3u   /* escalier : transition cluster */

/* Direction d'entree (utile pour placer le joueur) */
#define STATIC_ROOM_ENTRY_NORTH 0u
#define STATIC_ROOM_ENTRY_SOUTH 1u
#define STATIC_ROOM_ENTRY_EAST  2u
#define STATIC_ROOM_ENTRY_WEST  3u
#define STATIC_ROOM_ENTRY_NONE  0xFFu  /* pas d'entree connue, spawn centre */

/* ---- API ---- */

/*
 * Charge une fois les tiles de tileset_unit en VRAM et set les palettes
 * SCR1 (PAL_FLOOR + PAL_WALL) et SCR2 (PAL_DECO). A appeler avant
 * static_room_load().
 */
void static_room_loader_init_vram(void);

/*
 * Rend la salle room_idx sur SCR1/SCR2 et memorise la definition
 * courante pour les queries de collision. Clear SCR1/SCR2 avant dessin.
 * REGENERE le mobilier random (pushables / active_decor) -> ecrase l'etat
 * runtime existant. Utiliser static_room_redraw_current() au retour de
 * pause pour preserver les positions de caisses/vases poussees.
 */
void static_room_load(u8 room_idx);

/*
 * Redessine la salle courante (geometrie + mobilier) a partir de l'etat
 * runtime conserve. NE REGENERE PAS les pushables / active_decor :
 * les positions actuelles (potentiellement modifiees par push / spike
 * trap) sont preservees. A utiliser au retour de STATE_PAUSE /
 * STATE_MINIMAP / STATE_OPTIONS qui ecrasent la VRAM mais doivent
 * preserver l'etat gameplay. Sans effet si aucune salle n'est
 * actuellement chargee.
 */
void static_room_redraw_current(void);

/*
 * Collision d'une cellule en coords metatile (x, y).
 * Retourne une des valeurs STATIC_ROOM_COL_*.
 * Hors grille = SOLID.
 */
u8 static_room_collision_at(s8 x, s8 y);

/* Acces a la RoomDef courante (pour sockets enemies/items/stair). */
const StaticRoomDef *static_room_current(void);

/* Dimensions de la salle courante (raccourcis). */
u8 static_room_w(void);
u8 static_room_h(void);

/*
 * Position d'entree pour un entry_side donne (N/S/E/W).
 * Retourne 1 et remplit (*gx, *gy) avec la case juste a l'interieur de
 * la porte correspondante. Retourne 0 si pas d'entree sur ce cote
 * (fallback centre de la salle).
 */
u8 static_room_entry_position(u8 entry_side, u8 *gx, u8 *gy);

/*
 * MKD-5 v3 : scelle les portes du cote indique par seal_mask (bitmask
 * STATIC_ROOM_EXIT_N|E|S|W). Pour chaque bit set :
 *   - Le tile de la porte est remplace par le tile de mur correspondant
 *   - La collision sur cette case retourne SOLID
 * Le mask reste actif jusqu'au prochain static_room_load (qui le reset).
 *
 * Appele par le code de cluster (main.c) apres static_room_load pour
 * masquer les portes qui pointent vers un neighbor STAIR/NONE (i.e. pas
 * de room voisine dans le cluster).
 */
void static_room_seal_doors(u8 seal_mask);

/* =========================================================================
 * Pushables (STRAT-1) — Sokoban-style.
 *
 * Vases / caisses placees au runtime, deplacables en marchant contre.
 * Bloquent comme SOLID pour la collision (joueur ET enemies).
 * Ne droppent pas, ne se cassent pas. Consommables si pousses sur un
 * spike trap (gere par main.c, l'objet est retire de la liste).
 * ========================================================================= */
#define STATIC_ROOM_MAX_PUSHABLES 6u

#define PUSHABLE_TYPE_NONE   0u
#define PUSHABLE_TYPE_VASE   1u
#define PUSHABLE_TYPE_CAISSE 2u  /* asset = TILE_U_DECO_VASE en V1 (placeholder) */

/* Retourne le nombre de pushables actuellement places dans la room.
 * Utilise par dungeon_apply_lock_for_current_room pour ne pas placer
 * un trigger dans une room sans pushable disponible (= softlock). */
u8 static_room_pushable_count(void);

/* Cherche un pushable a (x, y). Retourne idx (0..count-1) ou 0xFFu. */
u8 static_room_pushable_at(u8 x, u8 y);

/*
 * Tente de pousser le pushable a (from_x, from_y) de (dx, dy).
 * Retourne :
 *   0 = pas de pushable a (from_x, from_y)
 *   1 = push reussi (pushable deplace + redraw effectue)
 *   2 = push bloque (case destination = mur / decor / autre pushable / void / stair)
 *
 * NOTE : ne check pas les enemies (responsabilite du caller dans main.c,
 * qui a access aux structures dungeon).
 */
u8 static_room_pushable_push(u8 from_x, u8 from_y, s8 dx, s8 dy);

/*
 * Retire un pushable (consomme par spike trap). Efface le tile SCR2 et
 * compacte le tableau. Pas d'effet si idx invalide.
 */
void static_room_pushable_remove(u8 idx);

/* =========================================================================
 * Locked door + pressure plate trigger (MKD-lock)
 *
 * Une room peut avoir AU PLUS une porte verrouillee, controlee par un
 * trigger (declencheur) place sur le sol. La porte verrouillee est dessinee
 * par-dessus une porte exit existante (overlay TILE_U_DOOR_N/W).
 *
 * Trigger actif (joueur / enemy / pushable dessus) -> porte s'ouvre via
 * animation 4 frames (closed 0 -> opening 1 -> opening 2 -> fully open 3).
 * Trigger relache -> animation inverse 3 -> 0.
 *
 * Collision a travers la cell de la porte verrouillee = SOLID tant que
 * frame < 3.
 *
 * Etat persiste par main.c (ClusterRoom), passe au loader via _init/_clear
 * a chaque entree de salle, et relu via les accesseurs avant exit.
 * ========================================================================= */

/* 5 etats visuels : frames 0..3 viennent de door_sheet (closed -> opening
 * progressif), frame 4 = "fully open" qui reutilise la porte existante
 * TILE_U_DOOR_N/W (la sheet n'inclut pas cette frame, le PNG s'arrete
 * avant). La porte bloque tant que frame < 4. */
#define LOCK_DOOR_FRAME_CLOSED  0u
#define LOCK_DOOR_FRAME_OPEN    4u

/* Initialise l'etat lock pour la salle courante.
 *   lock_dir = STATIC_ROOM_EXIT_N|E|S|W (la porte verrouillee) ou 0 = aucune
 *   tx, ty   = position metatile du trigger (ignoree si lock_dir=0)
 *   frame    = frame visible courante au moment de l'entree (0=fermee..3=ouverte)
 *   held     = 1 si quelque chose tient le trigger des l'entree */
void static_room_lock_init(u8 lock_dir, u8 tx, u8 ty, u8 frame, u8 held);

/* Equivalent a static_room_lock_init(0, 0, 0, 0, 0). */
void static_room_lock_clear(void);

/* Accesseurs pour persistance par cluster slot. */
u8 static_room_lock_dir(void);        /* 0 = aucun lock dans la salle */
u8 static_room_lock_trigger_x(void);
u8 static_room_lock_trigger_y(void);
u8 static_room_lock_frame(void);      /* 0..3 */
u8 static_room_lock_held(void);       /* 1 si trigger occupe */

/* Indique si quelque chose tient le trigger ; ajuste le target frame
 * (0 si held=0, 3 si held=1) et lance l'anim si necessaire. Idempotent. */
void static_room_lock_set_held(u8 held);

/* Avance l'anim d'un tick. A appeler chaque vsync depuis dungeon_update. */
void static_room_lock_tick(void);

/* Retourne 1 si (x,y) est la cell de la porte verrouillee ET pas
 * completement ouverte (frame < 3). Caller doit traiter comme SOLID. */
u8 static_room_lock_blocks_at(u8 x, u8 y);

/* Redessine la porte avec la frame courante + le trigger. Appele en
 * interne par set_held / tick + expose pour redraw post-pause / room_load. */
void static_room_lock_redraw(void);

/* MKD-lock helpers : selection d'une decor_anchor non utilisee par le
 * furnishing (totem / vase / caisse). A appeler APRES static_room_load
 * pour choisir un emplacement de trigger garanti libre.
 *
 * static_room_free_anchor_count : nombre d'anchors libres dans la salle
 *   courante.
 * static_room_get_free_anchor : retourne 1 + remplit (*x, *y) avec la
 *   n-ieme (idx-eme) anchor libre. 0 si idx >= count. */
u8 static_room_free_anchor_count(void);
u8 static_room_get_free_anchor(u8 idx, u8 *out_x, u8 *out_y);

/* MKD-lock helpers : type du pushable a une position (PUSHABLE_TYPE_*).
 * Retourne PUSHABLE_TYPE_NONE si aucun pushable a (x, y). */
u8 static_room_pushable_type_at(u8 x, u8 y);

/* MKD-lock : ajoute un pushable force (utilise pour restaurer une caisse
 * "sur le trigger" au retour dans une salle deja visitee). Si capacite
 * pleine ou pushable deja a (x, y), no-op. */
void static_room_pushable_add_at(u8 x, u8 y, u8 type);

#endif /* STATIC_ROOM_LOADER_H */
