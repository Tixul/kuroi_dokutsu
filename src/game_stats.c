/* game_stats.c
 * =============================================================================
 * Tables ROM des defs (enemies, items, loot, level-up, power-ups).
 * Editer ici pour TUNING — les changements affectent les rolls runtime sans
 * toucher au code de combat / inventaire / level-up.
 */

#include "game_stats.h"

/* =========================================================================
 * Enemies — stats par type
 *
 * Format : { max_hp, normal_min, normal_max, crit_min, crit_max,
 *            xp_drop, gold_avg, def }
 *
 * SLIME  : faible, base early-game
 * SKULL  : moyen, deplacement lineaire
 * FLAMME : moyen, comportement statique
 * HENT   : fort, gardien
 * ========================================================================= */
const EnemyStats NGP_FAR g_enemy_stats[ENEMY_TYPE_COUNT] = {
    /* SLIME  */ { 3u, 1u, 1u, 2u, 2u,  2u, 1u, 0u },
    /* SKULL  */ { 5u, 2u, 2u, 3u, 4u,  4u, 2u, 0u },
    /* FLAMME */ { 5u, 2u, 2u, 3u, 4u,  4u, 2u, 0u },
    /* HENT   */ { 6u, 2u, 4u, 4u, 6u,  6u, 3u, 1u }
};

const EnemyStats *game_stats_enemy(u8 type)
{
    if (type >= (u8)ENEMY_TYPE_COUNT) return 0;
    return &g_enemy_stats[type];
}

/* =========================================================================
 * Items database (vide pour l'instant - rempli par Phase 2 Loot loop)
 *
 * Convention : item_id 0 = empty slot (jamais reference dans le db).
 * Format : { name, type, atk, def, effect_id, effect_value, icon_tile }
 * ========================================================================= */
const ItemDef NGP_FAR g_item_db[] = {
    /* id 0 : slot vide (jamais reference, game_stats_item retourne NULL).
     * Format : { name, type, atk, def, effect_id, effect_value,
     *            effect_value_min, icon_tile } */
    /* 0 */ { "(none)",   ITEM_TYPE_NONE,       0u, 0u, EFFECT_NONE,        0u, 0u, 0u },
    /* 1 */ { "POTION",   ITEM_TYPE_CONSUMABLE, 0u, 0u, EFFECT_HEAL_HP,     8u, 5u, 0u },
    /* 2 */ { "ANTIDOTE", ITEM_TYPE_CONSUMABLE, 0u, 0u, EFFECT_CURE_STATUS, 0u, 0u, 0u }
};

const u8 g_item_db_count = 3u;

const ItemDef *game_stats_item(u8 item_id)
{
    if (item_id == 0u || item_id >= g_item_db_count) return 0;
    return &g_item_db[item_id];
}

/* =========================================================================
 * Loot tables (vide - Phase 2)
 * ========================================================================= */
const LootEntry NGP_FAR g_loot_table_chest[] = {
    { 0u, 1u, 0u }  /* placeholder */
};
const u8 g_loot_table_chest_count = 1u;

const LootEntry NGP_FAR g_loot_table_enemy[] = {
    { 0u, 1u, 0u }  /* placeholder */
};
const u8 g_loot_table_enemy_count = 1u;

/* =========================================================================
 * Player level-up curve
 *
 * Index = level courant. xp_needed = xp pour passer au level suivant.
 * Bonuses appliques au level UP (donc index = level a partir duquel on level
 * up vers index+1).
 *
 * Balance initial : xp lineaire +10 par level, bonus +2 HP / +1 ATK chaque
 * 2 levels, +1 DEF tous les 4 levels.
 * ========================================================================= */
const LevelUpEntry NGP_FAR g_level_curve[PLAYER_LEVEL_MAX] = {
    /* LV  xp_need hp atk def */
    /* 1*/ { 10u,  2u, 1u, 0u },  /* lv1 -> lv2 */
    /* 2*/ { 20u,  2u, 0u, 1u },
    /* 3*/ { 30u,  3u, 1u, 0u },
    /* 4*/ { 45u,  2u, 0u, 1u },
    /* 5*/ { 60u,  3u, 1u, 0u },
    /* 6*/ { 80u,  3u, 0u, 1u },
    /* 7*/ {100u,  4u, 1u, 0u },
    /* 8*/ {120u,  4u, 0u, 1u },
    /* 9*/ {150u,  5u, 1u, 1u },
    /*10*/ {180u,  5u, 1u, 0u },
    /*11*/ {210u,  5u, 0u, 1u },
    /*12*/ {240u,  6u, 1u, 0u },
    /*13*/ {255u,  6u, 0u, 1u },  /* xp_needed capped a 255 */
    /*14*/ {255u,  7u, 1u, 0u },
    /*15*/ {255u,  7u, 0u, 1u },
    /*16*/ {255u,  8u, 1u, 1u },
    /*17*/ {255u,  8u, 1u, 0u },
    /*18*/ {255u, 10u, 1u, 1u },
    /*19*/ {255u, 10u, 1u, 1u },
    /*20*/ {  0u,  0u, 0u, 0u }   /* max level — pas de level-up au-dela */
};

/* =========================================================================
 * Power-ups (choix fin de niveau)
 * ========================================================================= */
const PowerupDef NGP_FAR g_powerup_db[POWERUP_COUNT] = {
    /* POWERUP_HP_MAX     */ { "+10 HP MAX",  "Increase max HP",   10u, 0u, 10u },
    /* POWERUP_ATK        */ { "+2 ATK",      "Boost attack",       2u, 0u, 10u },
    /* POWERUP_DEF        */ { "+2 DEF",      "Boost defense",      2u, 0u,  8u },
    /* POWERUP_CRIT       */ { "+5% CRIT",    "Better crit rate",   5u, 1u,  6u },
    /* POWERUP_GOLD_DROP  */ { "GOLD x1.5",   "More gold drops",    0u, 2u,  4u },
    /* POWERUP_EXTRA_CHEST*/ { "+1 CHEST",    "Extra chest/floor",  0u, 3u,  3u },
    /* POWERUP_INV_SLOTS  */ { "+3 SLOTS",    "Bigger inventory",   3u, 2u,  3u },
    /* POWERUP_REVEAL_MAP */ { "MAP REVEAL",  "Pre-revealed map",   0u, 4u,  2u }
};
