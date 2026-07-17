/* player_state.h
 * =============================================================================
 * Etat runtime du joueur (RAM) : stats evolutives, inventaire, equipement,
 * status effects, resources (gold).
 *
 * Les DEFS immuables (stats par enemy type, tables loot, level-up curve) sont
 * dans game_stats.h.
 *
 * 1 instance globale g_player. ~50 octets sur 8KB RAM dispo.
 */

#ifndef PLAYER_STATE_H
#define PLAYER_STATE_H

#include "core/ngpc_hw.h"
#include "game_stats.h"

#define INVENTORY_SLOTS_BASE  12u   /* slots de base */
#define INVENTORY_SLOTS_MAX   16u   /* cap apres power-up "+3 SLOTS" */

/* TEMP (MKD-inv) : 1 = donne potion x3 + antidote x1 au reset, pour tester
 * l'inventaire tant que les coffres ne droppent pas. Passer a 0 quand le
 * loot des coffres existe. */
#ifndef MKD_DEBUG_START_ITEMS
#define MKD_DEBUG_START_ITEMS 1
#endif

/* =========================================================================
 * Inventory
 * ========================================================================= */
typedef struct {
    u8 item_id;     /* 0 = empty (cf g_item_db convention) */
    u8 count;       /* pour stackables (potions). 1 pour non-stack. */
} InventorySlot;

typedef struct {
    u8 weapon_id;   /* item_id, 0 = unarmed */
    u8 armor_id;    /* item_id, 0 = no armor */
} Equipment;

/* =========================================================================
 * Player state — la grande struct
 * ========================================================================= */
typedef struct {
    /* ---- Stats evolutives ---- */
    u8 level;            /* 1..PLAYER_LEVEL_MAX */
    u8 xp;               /* 0..xp_needed (curve) */
    u8 hp_max;           /* base + level bonuses + permanent powerups */
    u8 hp;               /* current HP */
    u8 atk_base;         /* before equipment + buffs */
    u8 def_base;
    u8 crit_chance;      /* % de chance de crit (0..100) */

    /* ---- Resources ---- */
    u8 gold;             /* 0..255 */

    /* ---- Permanent flags (set par power-ups, persiste tout le run) ---- */
    u8 powerup_flags;    /* bitmask POWERUP_FLAG_* */
    u8 inv_slots_total;  /* nombre de slots actifs (12 base, +3 si power-up) */

    /* ---- Equipment + inventory ---- */
    Equipment     equip;
    InventorySlot inv[INVENTORY_SLOTS_MAX];

    /* ---- Status effects (temporaire combat) ---- */
    u8 status_poison_timer;   /* ticks restants (0 = no effect) */
    u8 status_burn_timer;
    u8 buff_atk_timer;
    u8 buff_atk_value;
    u8 buff_def_timer;
    u8 buff_def_value;
} PlayerState;

/* Singleton (defini dans player_state.c). */
extern PlayerState g_player;

/* =========================================================================
 * API
 * ========================================================================= */

/* Reset pour un nouveau run (boot ou game over -> menu -> start). */
void player_state_reset(void);

/* Health management. damage_apply gere death (passe HP=0, pas de death seq ici). */
void player_state_apply_damage(u8 damage);
void player_state_heal(u8 amount);
u8   player_state_is_dead(void);            /* 1 si HP == 0 */

/* XP / Level. gain_xp gere les level-ups en cascade. */
void player_state_gain_xp(u8 amount);

/* Stats effectives (avec equipment + buffs). */
u8 player_state_atk_total(void);
u8 player_state_def_total(void);
u8 player_state_crit_chance(void);

/* Inventory. add retourne 1 si full (item non ajoute). */
u8   player_state_inv_add(u8 item_id, u8 count);
void player_state_inv_remove(u8 slot);
u8   player_state_inv_find_empty(void);     /* retourne slot ou 0xFF si full */

/* Consomme l'item du slot (applique son effet : heal aleatoire, cure status).
 * Retourne 1 si l'item a ete utilise+consomme, 0 sinon (slot vide, non
 * consommable, ou effet sans cible — ex antidote sans poison actif). */
u8   player_state_use_item(u8 inv_slot);

/* Equipement. equip_slot = inventaire slot a equiper (swap avec equip courant). */
void player_state_equip(u8 inv_slot);
void player_state_unequip_weapon(void);
void player_state_unequip_armor(void);

/* Power-ups (applique l'effet permanent). */
void player_state_apply_powerup(u8 powerup_type);

/* Status effects (decrement per turn). */
void player_state_tick_status(void);

#endif /* PLAYER_STATE_H */
