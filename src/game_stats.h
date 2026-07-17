/* game_stats.h
 * =============================================================================
 * Definitions immuables (ROM) de toutes les entites et items du jeu :
 *   - Stats enemies (par type)
 *   - Items database (weapons/armors/consumables/keys)
 *   - Loot tables (chest + enemy drops)
 *   - Player level-up curve
 *   - Power-ups (fin de niveau)
 *
 * Les TABLES sont en ROM (const NGP_FAR), accedees via accesseurs.
 * Le STATE runtime (HP courant, XP, inventaire) est dans player_state.h.
 */

#ifndef GAME_STATS_H
#define GAME_STATS_H

#include "core/ngpc_hw.h"  /* u8, u16, NGP_FAR */

/* =========================================================================
 * Enemies
 * ========================================================================= */

typedef enum {
    ENEMY_TYPE_SLIME = 0,
    ENEMY_TYPE_SKULL,
    ENEMY_TYPE_FLAMME,
    ENEMY_TYPE_HENT,
    ENEMY_TYPE_COUNT      /* sentinel — sert de borne pour les boucles */
    /* Futurs enemies (boss, mini-boss) : ajouter ICI puis remplir g_enemy_stats. */
} EnemyType;

typedef struct {
    u8 max_hp;
    u8 normal_min;       /* damage normal min */
    u8 normal_max;       /* damage normal max */
    u8 crit_min;         /* damage crit min */
    u8 crit_max;         /* damage crit max */
    u8 xp_drop;          /* XP gagne en tuant cet enemy */
    u8 gold_drop_avg;    /* gold moyen drop (0 = ne drop jamais) */
    u8 def;              /* defense reduisant degats subis (futur) */
} EnemyStats;

/* Table ROM : 1 entree par EnemyType (indexee par enum). */
extern const EnemyStats NGP_FAR g_enemy_stats[ENEMY_TYPE_COUNT];

/* Accesseur safe (retourne ptr NULL si type invalide). */
const EnemyStats *game_stats_enemy(u8 type);

/* =========================================================================
 * Items (loot drops, equipement, consumables)
 * ========================================================================= */

typedef enum {
    ITEM_TYPE_NONE = 0,
    ITEM_TYPE_WEAPON,
    ITEM_TYPE_ARMOR,
    ITEM_TYPE_CONSUMABLE,
    ITEM_TYPE_KEY
} ItemType;

/* Effets de consumables (utilises avec consume_effect dans player_state). */
typedef enum {
    EFFECT_NONE = 0,
    EFFECT_HEAL_HP,         /* effect_value = HP a restaurer */
    EFFECT_BUFF_ATK_TEMP,   /* effect_value = +ATK temporaire (timer combat) */
    EFFECT_BUFF_DEF_TEMP,
    EFFECT_CURE_STATUS      /* annule poison/burn */
} ItemEffect;

typedef struct {
    const char *name;        /* "Iron Sword" — affiche pickup popup, max 16 chars */
    u8 type;                 /* ItemType */
    u8 atk;                  /* 0 sauf weapon */
    u8 def;                  /* 0 sauf armor */
    u8 effect_id;            /* ItemEffect — sauf consumable */
    u8 effect_value;         /* parametre principal (heal MAX, buff value...) */
    u8 effect_value_min;     /* heal MIN pour heal aleatoire (0 = effet fixe) */
    u8 icon_tile;            /* (legacy : rendu inventaire via metasprite) */
} ItemDef;

extern const ItemDef NGP_FAR g_item_db[];
extern const u8 g_item_db_count;

/* IDs items (index dans g_item_db). 0 = slot vide. */
#define ITEM_ID_POTION    1u   /* heal aleatoire 5..8 HP */
#define ITEM_ID_ANTIDOTE  2u   /* annule poison/burn */

/* Accesseur safe (id 0 reserve pour "empty slot"). */
const ItemDef *game_stats_item(u8 item_id);

/* =========================================================================
 * Loot tables
 * ========================================================================= */

typedef struct {
    u8 item_id;          /* index dans g_item_db, 0 = no drop */
    u8 weight;           /* probabilite relative (somme libre) */
    u8 min_floor;        /* drop uniquement a partir de ce floor */
} LootEntry;

/* Table des items possibles dans un coffre. */
extern const LootEntry NGP_FAR g_loot_table_chest[];
extern const u8 g_loot_table_chest_count;

/* Table des items que les enemies peuvent dropper en mourant. */
extern const LootEntry NGP_FAR g_loot_table_enemy[];
extern const u8 g_loot_table_enemy_count;

/* =========================================================================
 * Player level-up curve
 * ========================================================================= */

#define PLAYER_LEVEL_MAX 20u  /* cap : limite raisonable pour balance */

typedef struct {
    u8 xp_needed;        /* xp pour passer du level (index) au level+1 */
    u8 hp_bonus;         /* +max_hp gagne au level-up */
    u8 atk_bonus;        /* +atk_base */
    u8 def_bonus;        /* +def_base */
} LevelUpEntry;

extern const LevelUpEntry NGP_FAR g_level_curve[PLAYER_LEVEL_MAX];

/* =========================================================================
 * Power-ups (choix fin de niveau)
 * ========================================================================= */

typedef enum {
    POWERUP_HP_MAX = 0,      /* +HP max permanent */
    POWERUP_ATK,             /* +ATK permanent */
    POWERUP_DEF,             /* +DEF permanent */
    POWERUP_CRIT,            /* +crit chance % */
    POWERUP_GOLD_DROP,       /* multiplier gold (flag) */
    POWERUP_EXTRA_CHEST,     /* +1 coffre garanti par floor (flag) */
    POWERUP_INV_SLOTS,       /* +N slots inventaire (rare) */
    POWERUP_REVEAL_MAP,      /* minimap pre-revelee (flag) */
    POWERUP_COUNT            /* sentinel */
} PowerupType;

/* Bitmask flags pour PowerupType qui modifient des flags plutot que stats. */
#define POWERUP_FLAG_GOLD_DROP    (1u << 0)
#define POWERUP_FLAG_EXTRA_CHEST  (1u << 1)
#define POWERUP_FLAG_REVEAL_MAP   (1u << 2)

typedef struct {
    const char *name;        /* "+10 HP MAX" */
    const char *desc;        /* "Increase max HP", max ~20 chars */
    u8 effect_value;         /* valeur appliquee (ex 10 pour +10 HP) */
    u8 min_floor;            /* debloquage par floor */
    u8 weight;               /* probabilite tirage */
} PowerupDef;

extern const PowerupDef NGP_FAR g_powerup_db[POWERUP_COUNT];

#endif /* GAME_STATS_H */
