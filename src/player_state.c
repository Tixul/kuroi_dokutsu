/* player_state.c
 * =============================================================================
 * Implementation runtime du joueur. State global g_player + API.
 *
 * Death sequence : apply_damage met HP=0 et marque is_dead, mais ne declenche
 * pas l'anim/timer de mort (c'est le caller dans main.c qui gere).
 */

#include "player_state.h"
#include "game_stats.h"
#include "core/ngpc_math.h"  /* ngpc_qrandom() pour le heal aleatoire (potion) */

/* =========================================================================
 * Singleton
 * ========================================================================= */
PlayerState g_player;

/* =========================================================================
 * Reset (boot ou nouveau run)
 *
 * Stats initiales :
 *   - level 1, 0 xp
 *   - HP max 10 (de base) — sera +N par power-ups
 *   - ATK 1, DEF 0
 *   - 0% crit
 *   - gold 0
 *   - inventaire vide, 12 slots de base
 *   - equipment vide
 *   - tous status timers a 0
 * ========================================================================= */
#define PLAYER_HP_BASE       10u
#define PLAYER_ATK_BASE      2u  /* MKD-depth : match PLAYER_ATTACK_POWER pour
                                  * que combat utilise g_player.atk_base
                                  * (et POWERUP_ATK affecte le combat). */
#define PLAYER_DEF_BASE      0u
#define PLAYER_CRIT_BASE     0u

void player_state_reset(void)
{
    u8 i;

    g_player.level = 1u;
    g_player.xp = 0u;
    g_player.hp_max = PLAYER_HP_BASE;
    g_player.hp = PLAYER_HP_BASE;
    g_player.atk_base = PLAYER_ATK_BASE;
    g_player.def_base = PLAYER_DEF_BASE;
    g_player.crit_chance = PLAYER_CRIT_BASE;

    g_player.gold = 0u;
    g_player.powerup_flags = 0u;
    g_player.inv_slots_total = INVENTORY_SLOTS_BASE;

    g_player.equip.weapon_id = 0u;
    g_player.equip.armor_id = 0u;

    for (i = 0u; i < (u8)INVENTORY_SLOTS_MAX; i++) {
        g_player.inv[i].item_id = 0u;
        g_player.inv[i].count = 0u;
    }

    g_player.status_poison_timer = 0u;
    g_player.status_burn_timer = 0u;
    g_player.buff_atk_timer = 0u;
    g_player.buff_atk_value = 0u;
    g_player.buff_def_timer = 0u;
    g_player.buff_def_value = 0u;

#if MKD_DEBUG_START_ITEMS
    /* TEMP : items de depart pour tester l'inventaire tant que les coffres
     * ne droppent pas encore (potion/antidote "non trouvables" autrement).
     * Mettre MKD_DEBUG_START_ITEMS a 0 une fois le loot des coffres branche. */
    (void)player_state_inv_add(ITEM_ID_POTION, 3u);
    (void)player_state_inv_add(ITEM_ID_ANTIDOTE, 1u);
#endif
}

/* =========================================================================
 * Health
 * ========================================================================= */
void player_state_apply_damage(u8 damage)
{
    if (damage >= g_player.hp) {
        g_player.hp = 0u;
        return;
    }
    g_player.hp = (u8)(g_player.hp - damage);
}

void player_state_heal(u8 amount)
{
    u8 new_hp;

    new_hp = (u8)(g_player.hp + amount);
    /* clamp overflow + cap a hp_max */
    if (new_hp < g_player.hp || new_hp > g_player.hp_max) {
        new_hp = g_player.hp_max;
    }
    g_player.hp = new_hp;
}

u8 player_state_is_dead(void)
{
    return (u8)(g_player.hp == 0u ? 1u : 0u);
}

/* =========================================================================
 * XP / Level
 *
 * gain_xp peut declencher plusieurs level-ups en cascade si gros gain.
 * Cap au max level (PLAYER_LEVEL_MAX). Bonus appliques via g_level_curve.
 * ========================================================================= */
void player_state_gain_xp(u8 amount)
{
    const LevelUpEntry *cur;
    u16 total;
    u8 need;

    if (g_player.level >= PLAYER_LEVEL_MAX)
        return;

    /* Somme en u16 pour gerer xp courant + amount > 255. */
    total = (u16)((u16)g_player.xp + (u16)amount);

    /* Cascade level-up tant que possible. */
    while (g_player.level < PLAYER_LEVEL_MAX) {
        cur = &g_level_curve[g_player.level - 1u];
        need = cur->xp_needed;
        if (need == 0u || total < (u16)need) {
            break;
        }
        /* Level up : applique bonuses, retire le cout xp, increment level. */
        total = (u16)(total - (u16)need);
        g_player.hp_max = (u8)(g_player.hp_max + cur->hp_bonus);
        /* Heal complet au level-up (convention classique). */
        g_player.hp = g_player.hp_max;
        g_player.atk_base = (u8)(g_player.atk_base + cur->atk_bonus);
        g_player.def_base = (u8)(g_player.def_base + cur->def_bonus);
        g_player.level = (u8)(g_player.level + 1u);
    }

    /* Stocke xp restant. Cap a 255 (struct field u8). */
    if (total > 255u) total = 255u;
    g_player.xp = (u8)total;
}

/* =========================================================================
 * Stats effectives (base + equipment + buffs)
 * ========================================================================= */
u8 player_state_atk_total(void)
{
    u16 v;
    const ItemDef *w;

    v = (u16)g_player.atk_base;

    w = game_stats_item(g_player.equip.weapon_id);
    if (w != 0) {
        v = (u16)(v + (u16)w->atk);
    }
    if (g_player.buff_atk_timer > 0u) {
        v = (u16)(v + (u16)g_player.buff_atk_value);
    }
    if (v > 255u) v = 255u;
    return (u8)v;
}

u8 player_state_def_total(void)
{
    u16 v;
    const ItemDef *a;

    v = (u16)g_player.def_base;

    a = game_stats_item(g_player.equip.armor_id);
    if (a != 0) {
        v = (u16)(v + (u16)a->def);
    }
    if (g_player.buff_def_timer > 0u) {
        v = (u16)(v + (u16)g_player.buff_def_value);
    }
    if (v > 255u) v = 255u;
    return (u8)v;
}

u8 player_state_crit_chance(void)
{
    return g_player.crit_chance;
}

/* =========================================================================
 * Inventory
 * ========================================================================= */
u8 player_state_inv_find_empty(void)
{
    u8 i;

    for (i = 0u; i < g_player.inv_slots_total; i++) {
        if (g_player.inv[i].item_id == 0u)
            return i;
    }
    return 0xFFu;
}

/* Ajoute `count` objets, UN PAR SLOT (pas de stacking : 1 slot = 1 objet).
 * Retourne 1 si l'inventaire s'est rempli avant d'avoir tout ajoute. */
u8 player_state_inv_add(u8 item_id, u8 count)
{
    u8 i;
    u8 slot;

    if (item_id == 0u || count == 0u)
        return 1u;

    if (game_stats_item(item_id) == 0)
        return 1u;

    for (i = 0u; i < count; i++) {
        slot = player_state_inv_find_empty();
        if (slot == 0xFFu)
            return 1u;  /* plein : objets restants non ajoutes */
        g_player.inv[slot].item_id = item_id;
        g_player.inv[slot].count = 1u;
    }
    return 0u;
}

void player_state_inv_remove(u8 slot)
{
    if (slot >= (u8)INVENTORY_SLOTS_MAX)
        return;
    g_player.inv[slot].item_id = 0u;
    g_player.inv[slot].count = 0u;
}

/* Utilise (consomme) l'item du slot. Retourne 1 si consomme, 0 sinon. */
u8 player_state_use_item(u8 inv_slot)
{
    const ItemDef *def;
    u8 item_id;
    u8 amount;
    u8 used;

    if (inv_slot >= (u8)INVENTORY_SLOTS_MAX)
        return 0u;

    item_id = g_player.inv[inv_slot].item_id;
    if (item_id == 0u)
        return 0u;

    def = game_stats_item(item_id);
    if (def == 0 || def->type != (u8)ITEM_TYPE_CONSUMABLE)
        return 0u;

    used = 0u;
    switch (def->effect_id) {
    case (u8)EFFECT_HEAL_HP:
        amount = def->effect_value;
        /* Heal aleatoire dans [min,max] si effect_value_min defini (potion). */
        if (def->effect_value_min != 0u &&
            def->effect_value_min <= def->effect_value) {
            u8 span = (u8)(def->effect_value - def->effect_value_min + 1u);
            amount = (u8)(def->effect_value_min + (u8)(ngpc_qrandom() % span));
        }
        player_state_heal(amount);
        used = 1u;
        break;
    case (u8)EFFECT_CURE_STATUS:
        /* Antidote : annule poison/burn. Ne se consomme QUE s'il y a un
         * status actif a soigner (evite de gaspiller l'item). */
        if (g_player.status_poison_timer > 0u || g_player.status_burn_timer > 0u) {
            g_player.status_poison_timer = 0u;
            g_player.status_burn_timer = 0u;
            used = 1u;
        }
        break;
    default:
        break;
    }

    if (used) {
        /* 1 slot = 1 objet : utiliser consomme l'objet -> slot vide. */
        player_state_inv_remove(inv_slot);
    }
    return used;
}

/* =========================================================================
 * Equipment
 *
 * equip(inv_slot) : si item est WEAPON/ARMOR, swap avec equip courant.
 * L'ancien equip retourne dans le slot d'inventaire.
 * ========================================================================= */
void player_state_equip(u8 inv_slot)
{
    const ItemDef *def;
    u8 item_id;
    u8 old_id;

    if (inv_slot >= (u8)INVENTORY_SLOTS_MAX)
        return;

    item_id = g_player.inv[inv_slot].item_id;
    if (item_id == 0u)
        return;

    def = game_stats_item(item_id);
    if (def == 0)
        return;

    if (def->type == (u8)ITEM_TYPE_WEAPON) {
        old_id = g_player.equip.weapon_id;
        g_player.equip.weapon_id = item_id;
        g_player.inv[inv_slot].item_id = old_id;
        g_player.inv[inv_slot].count = (u8)(old_id == 0u ? 0u : 1u);
    } else if (def->type == (u8)ITEM_TYPE_ARMOR) {
        old_id = g_player.equip.armor_id;
        g_player.equip.armor_id = item_id;
        g_player.inv[inv_slot].item_id = old_id;
        g_player.inv[inv_slot].count = (u8)(old_id == 0u ? 0u : 1u);
    }
}

void player_state_unequip_weapon(void)
{
    u8 slot;

    if (g_player.equip.weapon_id == 0u)
        return;

    slot = player_state_inv_find_empty();
    if (slot == 0xFFu)
        return;  /* inventaire plein : ne deequip pas */

    g_player.inv[slot].item_id = g_player.equip.weapon_id;
    g_player.inv[slot].count = 1u;
    g_player.equip.weapon_id = 0u;
}

void player_state_unequip_armor(void)
{
    u8 slot;

    if (g_player.equip.armor_id == 0u)
        return;

    slot = player_state_inv_find_empty();
    if (slot == 0xFFu)
        return;

    g_player.inv[slot].item_id = g_player.equip.armor_id;
    g_player.inv[slot].count = 1u;
    g_player.equip.armor_id = 0u;
}

/* =========================================================================
 * Power-ups (fin de niveau)
 * ========================================================================= */
void player_state_apply_powerup(u8 powerup_type)
{
    const PowerupDef *p;

    if (powerup_type >= (u8)POWERUP_COUNT)
        return;

    p = &g_powerup_db[powerup_type];

    switch (powerup_type) {
    case (u8)POWERUP_HP_MAX:
        g_player.hp_max = (u8)(g_player.hp_max + p->effect_value);
        g_player.hp = (u8)(g_player.hp + p->effect_value);
        if (g_player.hp > g_player.hp_max) g_player.hp = g_player.hp_max;
        break;
    case (u8)POWERUP_ATK:
        g_player.atk_base = (u8)(g_player.atk_base + p->effect_value);
        break;
    case (u8)POWERUP_DEF:
        g_player.def_base = (u8)(g_player.def_base + p->effect_value);
        break;
    case (u8)POWERUP_CRIT:
        {
            u16 c;
            c = (u16)((u16)g_player.crit_chance + (u16)p->effect_value);
            if (c > 100u) c = 100u;
            g_player.crit_chance = (u8)c;
        }
        break;
    case (u8)POWERUP_GOLD_DROP:
        g_player.powerup_flags |= POWERUP_FLAG_GOLD_DROP;
        break;
    case (u8)POWERUP_EXTRA_CHEST:
        g_player.powerup_flags |= POWERUP_FLAG_EXTRA_CHEST;
        break;
    case (u8)POWERUP_INV_SLOTS:
        {
            u16 s;
            s = (u16)((u16)g_player.inv_slots_total + (u16)p->effect_value);
            if (s > (u16)INVENTORY_SLOTS_MAX) s = (u16)INVENTORY_SLOTS_MAX;
            g_player.inv_slots_total = (u8)s;
        }
        break;
    case (u8)POWERUP_REVEAL_MAP:
        g_player.powerup_flags |= POWERUP_FLAG_REVEAL_MAP;
        break;
    default:
        break;
    }
}

/* =========================================================================
 * Status effects (tick par turn)
 *
 * Applique le dommage poison/burn puis decremente timer.
 * Buffs : decremente timer (l'effet est applique au calcul de atk/def_total).
 * ========================================================================= */
#define POISON_TICK_DAMAGE  1u
#define BURN_TICK_DAMAGE    1u

void player_state_tick_status(void)
{
    if (g_player.status_poison_timer > 0u) {
        player_state_apply_damage(POISON_TICK_DAMAGE);
        g_player.status_poison_timer = (u8)(g_player.status_poison_timer - 1u);
    }
    if (g_player.status_burn_timer > 0u) {
        player_state_apply_damage(BURN_TICK_DAMAGE);
        g_player.status_burn_timer = (u8)(g_player.status_burn_timer - 1u);
    }
    if (g_player.buff_atk_timer > 0u) {
        g_player.buff_atk_timer = (u8)(g_player.buff_atk_timer - 1u);
        if (g_player.buff_atk_timer == 0u) g_player.buff_atk_value = 0u;
    }
    if (g_player.buff_def_timer > 0u) {
        g_player.buff_def_timer = (u8)(g_player.buff_def_timer - 1u);
        if (g_player.buff_def_timer == 0u) g_player.buff_def_value = 0u;
    }
}
