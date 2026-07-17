/*
 * main.c - NgpCraft_base_template
 *
 * Part of NgpCraft_base_template (MIT License)
 *
 * Intro screen (skippable with A) then dual-layer menu + BGM loop.
 *
 * Sound data : sound/sound_data.c  (pulls in "sound_sample.c")
 * Instruments: src/audio/sounds.c includes "sound_sample_instruments.c"
 *              (see docs/SOUND_DRIVER_REF.md — Tracker Export Integration)
 * Intro image : GraphX/intro_ngpc_craft_png.c/.h
 * Menu image  : GraphX/menu_kuroi_dokutsu.c/.h
 *
 * ---- HOW TO REPLACE THIS DEMO WITH YOUR OWN GAME ----
 *
 * 1. BGM: replace "sound_sample.c" in sound/ with your export.
 *         Replace "sound_sample_instruments.c" with the companion file from
 *         the same hybrid export.
 *
 * 2. Intro: swap intro_ngpc_craft_png with your own image (same pipeline:
 *           ngpc_tilemap.py PNG -> .c/.h, then NGP_TILEMAP_BLIT_SCR1).
 *           Or remove STATE_INTRO entirely and start at your title screen.
 *
 * 3. Game entry: replace STATE_MENU and menu_init with
 *                your own state machine (title, game, pause, etc.).
 *
 * 4. Cleanup: remove #include "sounds.h", sound_data.h, intro_ngpc_craft_png.h
 *             and the corresponding state functions once you no longer need them.
 * ------------------------------------------------------
 */

#include "ngpc_hw.h"
#include "carthdr.h"
#include "ngpc_sys.h"
#include "ngpc_gfx.h"
#include "ngpc_sprite.h"
#include "ngpc_metasprite.h"
#include "ngpc_input.h"
#include "ngpc_timing.h"
#include "ngpc_math.h"
#include "ngpc_text.h"
#include "ngpc_tilemap_blit.h"
#include "sounds.h"

#include "../sound/sound_data.h"
#include "../GraphX/intro_ngpc_craft_png.h"
#include "../GraphX/ahchay_font.h"
#include "../GraphX/menu_kuroi_dokutsu.h"
#include "../GraphX/menu_level_select.h"
#include "../GraphX/menu_level_pointer_mspr.h"
#include "../GraphX/menu_level_lock_mspr.h"
#include "../GraphX/menu_select_start.h"
#include "../GraphX/menu_select_continue.h"
#include "../GraphX/menu_select_options.h"
#include "../GraphX/salle_01_map.h"
#include "../GraphX/salle_01_col.h"
#include "../GraphX/salle_02_map.h"
#include "../GraphX/salle_02_col.h"
#include "../GraphX/player_topdown_mspr.h"
#include "../GraphX/lime_sheet_mspr.h"
#include "../GraphX/skull_mspr.h"
#include "../GraphX/flamme_mspr.h"
#include "../GraphX/hent_mspr.h"
#include "../GraphX/selecteur_mspr.h"
#include "../GraphX/effect_attaque_mspr.h"
#include "../GraphX/effect_attaque_crit_mspr.h"
#include "../GraphX/menu_pause.h"
#include "../GraphX/select_menu_pause_mspr.h"
#include "../GraphX/hud_mspr.h"
#include "../GraphX/hud_bg.h"
#include "../GraphX/skull_death_mspr.h"
#include "../GraphX/game_over_screen.h"
#include "../GraphX/inventaire.h"  /* MKD-inv : fond ecran inventaire (ITEM pause) */
#include "../GraphX/inventaire_selector_item_mspr.h"  /* MKD-inv : cadre selecteur item */
#include "../GraphX/item_potion_mspr.h"    /* MKD-inv : icone potion (heal 5-8) */
#include "../GraphX/item_antidote_mspr.h"  /* MKD-inv : icone antidote (cure poison) */
#include "../GraphX/coffre_mspr.h"         /* MKD-chest : coffre donjon (loot aleatoire) */
#include "static_room_bank.h"
#include "static_room_loader.h"
#include "game_stats.h"
#include "player_state.h"
#include "../GraphX/tiles_unit.h"  /* MKD-5 : tiles minimap (FLOOR/PILLAR/STAIR/DOOR) */
#include "../GraphX/map_room.h"               /* MKD-5 v2 : 3x3 room icon */
#include "../GraphX/map_jonction.h"           /* MKD-5 v2 : link horizontal */
#include "../GraphX/map_jonction_v.h"         /* MKD-5 v2 : link vertical (rotated 90deg) */
#include "../GraphX/map_player_position_mspr.h" /* MKD-5 v2 : player marker sprite */
#include "fx/ngpc_palfx.h"
#include "fx/ngpc_raster.h"  /* ngpc_raster_init() : SWI 1 BIOS_INTLVSET pour Timer0 IRQ level 4 */
#include "core/ngpc_vramq.h"  /* ngpc_vramq_flush() pour notre ISR VBlank custom */
#include "core/ngpc_flash.h"  /* SAVE-2 : flash save persistance bonus + unlocks */

/* ---- Game states ---- */

typedef enum {
    STATE_INTRO,
    STATE_MENU,
    STATE_LEVEL_SELECT,
    STATE_ROOM,
    STATE_PAUSE,
    STATE_DUNGEON,
    STATE_GAME_OVER,
    STATE_MINIMAP,  /* MKD-5 : vue cluster, entree depuis pause (TBD) */
    STATE_OPTIONS,  /* MKD-8 v2 : seed editor + futures options */
    STATE_VICTORY,  /* MKD-depth : depth_target atteint, screen stats du run */
    STATE_UPGRADE_CHOICE,  /* MKD-depth : apres victory, 3 cards Force/Vie/Chance */
    STATE_INVENTORY  /* MKD-inv : ecran inventaire, entree depuis pause (ITEM) */
} GameState;

typedef enum {
    MENU_SEL_START,
    MENU_SEL_CONTINUE,
    MENU_SEL_OPTIONS,
    MENU_SEL_COUNT
} MenuSelection;

typedef enum {
    PAUSE_SEL_ITEM = 0,
    PAUSE_SEL_MAP,        /* MKD-5 : ouvre STATE_MINIMAP */
    PAUSE_SEL_SAVE,
    PAUSE_SEL_QUIT,
    PAUSE_SEL_COUNT
} PauseSelection;

typedef enum {
    PLAYER_DIR_DOWN,
    PLAYER_DIR_UP,
    PLAYER_DIR_LEFT,
    PLAYER_DIR_RIGHT
} PlayerDirection;

typedef enum {
    ROOM_TURN_WAIT_INPUT,
    ROOM_TURN_PLAYER_ANIM,
    ROOM_TURN_ATTACK_EFFECT,
    ROOM_TURN_DAMAGE_POPUP,
    ROOM_TURN_AFTER_PLAYER_ATTACK_DELAY,
    ROOM_TURN_ENEMY_TURNS,
    ROOM_TURN_ENEMY_ANIM
} RoomTurnState;

typedef enum {
    TURN_ACTION_NONE,
    TURN_ACTION_MOVE,
    TURN_ACTION_ATTACK,
    TURN_ACTION_USE_ITEM,
    TURN_ACTION_WAIT,
    TURN_ACTION_INTERACT
} TurnActionType;

typedef enum {
    STATIC_ROOM_01 = 0,
    STATIC_ROOM_02,
    STATIC_ROOM_COUNT
} StaticRoomId;

typedef enum {
    ROOM_ENTRY_DEFAULT = 0,
    ROOM_ENTRY_FROM_SOUTH,
    ROOM_ENTRY_FROM_NORTH,
    ROOM_ENTRY_FROM_STAIR
} StaticRoomEntry;

typedef struct {
    const u8 *map;
    u8 map_w;
    u8 map_h;
} RoomCollision;

typedef enum {
    ATTACK_EFFECT_NONE,
    ATTACK_EFFECT_PLAYER,
    ATTACK_EFFECT_ENEMY
} AttackEffectSource;

static GameState s_state = STATE_INTRO;
/* MKD-5 : declare early so pause_update can set it before STATE_MINIMAP transition. */
static GameState s_minimap_return_state = STATE_DUNGEON;
/* Etat d'ou la pause a ete ouverte. Default STATE_ROOM pour back-compat. */
static GameState s_pause_return_state = STATE_ROOM;
/* MKD-8 v2 : seed du donjon. Persistante entre clusters tant que le user ne
 * la change pas via le menu Options. 0 = "auto" -> cluster_generate prend
 * ngpc_qrandom() comme avant. >0 = seed forcee par l'user, partageable. */
static u8 g_dungeon_seed = 0u;
/* MKD-3-A : seed utilisee par le dernier cluster_generate (affichee dans
 * pause/options + lue par static_room_loader.c pour le furnishing). NON-static
 * exprès car referenced par extern depuis static_room_loader.c. */
u8 g_last_cluster_seed = 0u;

/* MKD-depth : profondeur finale du donjon, tiree par seed dans [8..14] au
 * 1er cluster_generate. depth_current commence a 1 (1er cluster) et
 * incremente a chaque descente (stair PAD_A / void). Atteindre target
 * declenche STATE_VICTORY. Le joueur voit son depth courant dans le HUD
 * mais PAS le target -> tension entretenue. */
#define DEPTH_TARGET_MIN  8u
#define DEPTH_TARGET_MAX  14u
static u8 g_depth_target  = 10u;  /* fallback, ecrase au dungeon_init */
static u8 g_depth_current = 1u;

/* Stats du run, reset au dungeon_init. Affiches sur victory screen. */
static u8  g_run_kills = 0u;
static u16 g_run_turns = 0u;

/* MKD-depth : nombre de kills minimum sur un run pour debloquer le donjon
 * suivant dans le level select. Verifie a STATE_VICTORY entry. */
#define UNLOCK_KILLS_REQUIRED  6u

/* MKD-misc : forward decls pour la font opaque (impl en fin de fichier). */
static void hud_font_install_opaque_variant(void);
static void hud_font_install_palettes(u8 plane);
static void hud_text_print(u8 plane, u8 x, u8 y, const char *str);
static void hud_text_print_num(u8 plane, u8 x, u8 y, u16 value, u8 digits);

/* MKD-3-B-1 : RNG xorshift16 local pour cluster_generate, decouple du qrandom
 * global qui depend de l'entropy boot. Avec g_dungeon_seed fixe, la sequence
 * produite est totalement deterministique -> cluster + enemies reproductibles
 * a 100% (utile pour partage de seed et tests offline). */
static u16 s_cluster_rng_state = 1u;

static void cluster_rng_seed(u16 s)
{
    s_cluster_rng_state = s ? s : 0x1A3Cu;
}

static u8 cluster_rng_u8(void)
{
    u16 x = s_cluster_rng_state;
    x ^= (u16)(x << 7);
    x ^= (u16)(x >> 9);
    x ^= (u16)(x << 8);
    s_cluster_rng_state = x;
    return (u8)(x >> 8);
}

/* MKD-3-B-2 : bounds enemy count par semantic_role.
 * Le furnishing tire un count dans [min, max] selon le role de la room,
 * ce qui rend la densite cohrente avec le tag de la salle (ENTRY/SAFE
 * vide, COMBAT_HEAVY 2-3 enemies). Bounds clampes a max_avail (= min de
 * enemy_spawn_count + CLUSTER_MAX_ACTORS_PER_ROOM). */
static u8 cluster_enemy_min_for_role(u8 sem_role)
{
    if (sem_role == STATIC_ROOM_SEMANTIC_ENTRY)        return 0u;
    if (sem_role == STATIC_ROOM_SEMANTIC_SAFE)         return 0u;
    if (sem_role == STATIC_ROOM_SEMANTIC_TRANSIT)      return 0u;
    if (sem_role == STATIC_ROOM_SEMANTIC_COMBAT_LIGHT) return 1u;
    if (sem_role == STATIC_ROOM_SEMANTIC_COMBAT_HEAVY) return 2u;
    if (sem_role == STATIC_ROOM_SEMANTIC_TREASURE)     return 1u;
    if (sem_role == STATIC_ROOM_SEMANTIC_STAIR)        return 1u;
    if (sem_role == STATIC_ROOM_SEMANTIC_SECRET)       return 1u;
    return 0u;
}

static u8 cluster_enemy_max_for_role(u8 sem_role)
{
    if (sem_role == STATIC_ROOM_SEMANTIC_ENTRY)        return 0u;
    if (sem_role == STATIC_ROOM_SEMANTIC_SAFE)         return 0u;
    if (sem_role == STATIC_ROOM_SEMANTIC_TRANSIT)      return 1u;
    if (sem_role == STATIC_ROOM_SEMANTIC_COMBAT_LIGHT) return 2u;
    if (sem_role == STATIC_ROOM_SEMANTIC_COMBAT_HEAVY) return 3u;
    if (sem_role == STATIC_ROOM_SEMANTIC_TREASURE)     return 2u;
    if (sem_role == STATIC_ROOM_SEMANTIC_STAIR)        return 2u;
    if (sem_role == STATIC_ROOM_SEMANTIC_SECRET)       return 1u;
    return 1u;
}
static MenuSelection s_menu_selection = MENU_SEL_START;
static MenuSelection s_menu_last_selection = MENU_SEL_COUNT;
static RoomTurnState s_room_turn_state = ROOM_TURN_WAIT_INPUT;
static TurnActionType s_turn_action = TURN_ACTION_NONE;
static PlayerDirection s_player_dir = PLAYER_DIR_DOWN;
static u8 s_player_gx = 5u;
static u8 s_player_gy = 6u;
static u8 s_player_target_gx = 5u;
static u8 s_player_target_gy = 6u;
static s16 s_player_x = 80;
static s16 s_player_y = 96;
static s8 s_player_step_x = 0;
static s8 s_player_step_y = 0;
static u8 s_player_anim_frame = 1u;
static u8 s_player_anim_timer = 0u;
/* HP joueur migre vers g_player.hp (player_state.h). PLAYER_MAX_HP =>
 * g_player.hp_max (initialise par player_state_reset). */
static u8 s_enemy_active = 1u;
static u8 s_enemy_hp = 6u;
static PlayerDirection s_enemy_dir = PLAYER_DIR_LEFT;
static u8 s_enemy_gx = 8u;
static u8 s_enemy_gy = 6u;
static u8 s_enemy_target_gx = 8u;
static u8 s_enemy_target_gy = 6u;
static s16 s_enemy_x = 128;
static s16 s_enemy_y = 96;
static s8 s_enemy_step_x = 0;
static s8 s_enemy_step_y = 0;
static u8 s_enemy_anim_frame = 1u;
static u8 s_enemy_anim_timer = 0u;
static u8 s_selector_visible = 0u;
static u8 s_selector_gx = 0u;
static u8 s_selector_gy = 0u;
static u8 s_attack_effect_visible = 0u;
static u8 s_attack_effect_gx = 0u;
static u8 s_attack_effect_gy = 0u;
static u8 s_attack_effect_timer = 0u;
static u8 s_attack_effect_visual = 0u;
static AttackEffectSource s_attack_effect_source = ATTACK_EFFECT_NONE;
static u8 s_damage_popup_visible = 0u;
static u8 s_damage_popup_gx = 0u;
static u8 s_damage_popup_gy = 0u;
static u8 s_damage_popup_value = 0u;
static u8 s_damage_popup_timer = 0u;
static AttackEffectSource s_damage_popup_source = ATTACK_EFFECT_NONE;
/* Hit fatal sur player : on remplace le nombre par un sprite skull au-dessus
 * du joueur (cf damage_popup_draw / dung_popup_draw). Le flag est set par
 * popup_start si source=ENEMY et player_hp arrive a 0. */
static u8 s_damage_popup_is_fatal = 0u;
#define SKULL_DEATH_SPR_BASE 60u  /* OAM 60..63 (libre dans tous etats) */
static u8 s_after_player_attack_delay = 0u;
#define HUD_BG_SPLIT_LINE 136u   /* SCREEN_H - 16 : debut zone HUD */
/* HUD pose en bas du tilemap SCR2 32x32 (tile_y=30/31) plutot qu'en
 * haut (tile_y=17/18) pour eviter :
 *   1) le "fantome" du label PV: qui apparaissait au milieu d'ecran
 *      quand cam_y atteignait la valeur qui exposait tile_y=17
 *   2) les decos vase/totem placees aux world tile_y=17/18 dans certaines
 *      salles dungeon, qui s'affichaient figees dans la zone HUD
 * La zone HUD utilise donc scroll_y = 30*8 - 136 = 104 pour amener
 * tile_y=30 sur la scanline 136. cam_y_max en gameplay << 30*8 - 152,
 * donc tile_y=30 n'est JAMAIS visible dans la zone scrollee. */
#define HUD_BG_TILE_Y     30u
#define HUD_BG_SCROLL_Y   104u   /* HUD_BG_TILE_Y * 8 - HUD_BG_SPLIT_LINE */

/* ===== HUD split v4 (2026-05-17) : ISR Timer0 mini + apply en main loop =====
 *
 * Historique (cf hud_raster_update + main loop apres ngpc_vsync) :
 * - v0 (origine) : scroll table 152 entries via ngpc_raster.c. ISR HBlank fire
 *     chaque scanline -> ~9120 IRQ/s -> drop FPS hardware (~1 fps).
 * - v1 : 2 splits via ngpc_raster_chain (CPU split Sonic-style). FPS OK mais
 *     glitch caisses/decor zone gameplay car split[0] a ligne 0 et l'ISR
 *     debordait du budget HBlank (4 writes/split).
 * - v2 : 1 split (juste celui a ligne 136). Scroll cam pose via ngpc_gfx_scroll
 *     avant arm. Caisses OK mais HUD glitche haut+bas car `rchain_arm` etait
 *     appele depuis main loop apres l'update -> timing variable selon duree
 *     update, parfois split fire en VBlank (rate) parfois trop tot (HUD ecrit
 *     sur zone gameplay top).
 * - v3 : ISR VBlank custom override pour pousser shadow scroll + arm timer
 *     en tete VBlank ISR. Mais override de HW_INT_VBL casse l'emulateur NeoPop
 *     (probable bug emu : ne pas relire HW_INT_VBL apres ecriture, ou type
 *     mismatch cast). HUD invisible sur emu.
 * - v4 (active) : on garde l'ISR Timer0 mini (timer-driven, write 2 regs +
 *     stop), mais l'arm + push shadow se font dans le MAIN LOOP juste apres
 *     ngpc_vsync() = juste apres VBlank ISR finit = scanline ~0. TREG0 = 136
 *     fire au split scanline 136. Marche emu+hardware sans override d'ISR
 *     systeme.
 *
 * Tradeoff vs v3 : si l'overhead main loop entre vsync et arm est de N
 * scanlines, le split fire a 136+N. N est genre 1-2 scanlines -> jitter
 * neglige.
 *
 * Shadows : dungeon_update_camera() ecrit ces u8 chaque frame. La main loop
 * les pousse aux registres HW juste apres vsync (latch K2GE a scanline 0). */
static volatile u8 s_dung_scroll_scr1_x = 0u;
static volatile u8 s_dung_scroll_scr1_y = 0u;
static volatile u8 s_dung_scroll_scr2_x = 0u;
static volatile u8 s_dung_scroll_scr2_y = 0u;
static volatile u8 s_dung_hud_split_armed = 0u;

/* Nombre de HBlanks entre l'arm (juste apres vsync = scanline ~0) et le fire
 * du split HUD. = scanline cible 136. Si jitter visible sur hardware, ajuster. */
#define HUD_SPLIT_TREG0  136u

static void __interrupt isr_hud_split(void)
{
    /* ISR minimaliste : 2 writes I/O + stop timer. Doit tenir dans budget
     * HBlank ~30 cycles (cf EFFECTS.md §1.3). */
    HW_SCR2_OFS_X = 0u;
    HW_SCR2_OFS_Y = (u8)HUD_BG_SCROLL_Y;
    HW_TRUN &= (u8)~0x01u;  /* stop Timer0 jusqu'au prochain arm */
}

/* Pousse shadow scroll vers registres HW + arme Timer0 pour fire a scanline
 * HUD_SPLIT_TREG0. A appeler depuis le main loop juste apres ngpc_vsync()
 * pour que ce code execute en scanline 0 ou tres tot dans la zone visible. */
static void hud_apply_scroll_and_arm(void)
{
    HW_SCR1_OFS_X = s_dung_scroll_scr1_x;
    HW_SCR1_OFS_Y = s_dung_scroll_scr1_y;
    HW_SCR2_OFS_X = s_dung_scroll_scr2_x;
    HW_SCR2_OFS_Y = s_dung_scroll_scr2_y;
    HW_TRUN  &= (u8)~0x01u;
    HW_TREG0  = HUD_SPLIT_TREG0;
    HW_TRUN  |= 0x01u;
}

/* VRAM tile slots du bandeau HUD.png (9 tiles uniques). On choisit 420
 * pour etre apres tout asset gameplay : font (32..127), salle_01/02
 * (200..265), player_topdown (256..309), tileset_unit dungeon (310..381),
 * et l'ancien hud_mspr_tile_base (400 -- non utilise apres refactor mais
 * on l'evite par precaution). 9 tiles -> 420..428. */
#define HUD_BG_TILE_BASE      420u
/* Slot SCR2 palette dedie au bandeau (0=font ahchay, 1=PAL_DECO, 2=libre). */
#define HUD_BG_PAL_SCR2       2u
/* Death sequence : 0 = idle, 1 = waiting 2sec, 2 = fading to black.
 * Pendant phases 1+2 le gameplay (room/dungeon) est mis en pause. A la
 * fin du fade, transition vers STATE_GAME_OVER. */
static u8 s_death_phase = 0u;
static u8 s_death_timer = 0u;
#define DEATH_WAIT_FRAMES   120u  /* 2 sec @ 60fps */
#define DEATH_FADE_SPEED    4u    /* frames par step (fade en ~64 frames) */
#define GAME_OVER_TILE_BASE 128u  /* hors plage font (32..127) */
/* MKD-inv : ecran inventaire = overlay non-gameplay (comme pause/game over).
 * 77 tiles uniques -> 128..204, hors plage font, VRAM rechargee au retour
 * vers le donjon/salle. */
#define INVENTORY_TILE_BASE 128u
static u8 s_level_select_index = 0u;
static u8 s_level_select_unlocked[4] = { 1u, 0u, 0u, 0u };
static u8 s_level_select_pointer_x = 14u;
static u8 s_level_select_pointer_y = 106u;

/* ===== SAVE-2 (2026-05-17 v2) : pattern StarGunner shmup_profile.c =========
 *
 * Refactor sur le pattern HW-validated de StarGunner_save_lib_test :
 *   - s_mkd_save = singleton RAM, source de verite du save courant.
 *   - mkd_save_init() au boot : load+validate, sinon defaults.
 *   - mkd_save_commit() : packe l'etat RAM vers s_mkd_save + checksum + flash.
 *
 * Triggers : commit sur STATE_VICTORY entry (preserve bonus + unlocks +
 * records) et sur game over (records preserves, bonus+unlocks RESET).
 * Design rogue-like dur : la mort efface la meta-progression.
 *
 * Format (512 bytes, SAVE_SIZE = HW-validated):
 *   offset 0..3  : magic { 0xCA, 0xFE, 0x20, 0x26 } (oblige par ngpc_flash.h)
 *   offset 4     : version
 *   offset 5     : unlocked_levels bitmask
 *   offset 6..11 : bonus + records + last_seed
 *   offset 12..510 : padding zero
 *   offset 511   : checksum (sum bytes 0..510 XOR 0x5A, pattern shmup_profile.c)
 *
 * NB : si NGP_ENABLE_FLASH_SAVE=0, toutes les fonctions ngpc_flash_* sont
 * no-ops -> code safe a compiler tel quel. */
/* IMPORTANT : checksum est place AVANT le padding final (pas en derniere
 * position) pour avoir un offset deterministe independant de tout padding
 * compilo eventuel sur la struct entiere. Pattern shmup_profile.c (StarGunner)
 * HW-validated : checksum a offset 12, padding[SAVE_SIZE - 13] termine. */
typedef struct {
    u8 magic[4];                /* 0..3  : {0xCA, 0xFE, 0x20, 0x26} */
    u8 version;                 /* 4 */
    u8 unlocked_levels;         /* 5 */
    u8 atk_base;                /* 6 */
    u8 hp_max;                  /* 7 */
    u8 crit_chance;             /* 8 */
    u8 best_run_kills;          /* 9 */
    u8 best_run_floor;          /* 10 */
    u8 last_seed;               /* 11 */
    u8 checksum;                /* 12 : offset FIXE, deterministe */
    u8 padding[SAVE_SIZE - 13]; /* 13..(SAVE_SIZE-1) : zero-filled */
} MkdSaveData;

#define MKD_SAVE_MAGIC_0       0xCAu
#define MKD_SAVE_MAGIC_1       0xFEu
#define MKD_SAVE_MAGIC_2       0x20u
#define MKD_SAVE_MAGIC_3       0x26u
#define MKD_SAVE_VERSION       1u
#define MKD_SAVE_CHECKSUM_XOR  0x5Au

/* Bounds raisonnables pour validation (cf STORAGE.md §4.1). */
#define MKD_SAVE_BOUND_ATK_MAX   100u
#define MKD_SAVE_BOUND_HP_MAX    200u
#define MKD_SAVE_BOUND_CRIT_MAX  100u

/* Singleton RAM : source de verite du save courant. Init au boot, modifie
 * par mkd_save_commit() avant chaque flash write. Pattern shmup_profile.c
 * s_save. */
static MkdSaveData s_mkd_save;

/* Checksum sum bytes 0..(SAVE_SIZE-2) XOR 0x5A. Le byte checksum (offset 12)
 * tombe dans la sum, on soustrait sa valeur stockee pour que le digest soit
 * deterministe. Pattern shmup_profile.c (StarGunner) HW-validated. */
static u8 mkd_save_checksum_compute(const MkdSaveData *s)
{
    const u8 *raw = (const u8 *)s;
    u8 sum = 0u;
    u16 i;

    for (i = 0u; i < (u16)(SAVE_SIZE - 1u); i++) {
        sum = (u8)(sum + raw[i]);
    }
    sum = (u8)(sum - s->checksum);
    return (u8)(sum ^ MKD_SAVE_CHECKSUM_XOR);
}

/* Validation : magic + version + bounds + checksum (STORAGE.md §4.1). */
static u8 mkd_save_is_valid(const MkdSaveData *s)
{
    if (s->magic[0] != MKD_SAVE_MAGIC_0 || s->magic[1] != MKD_SAVE_MAGIC_1 ||
        s->magic[2] != MKD_SAVE_MAGIC_2 || s->magic[3] != MKD_SAVE_MAGIC_3) {
        return 0u;
    }
    if (s->version != MKD_SAVE_VERSION) return 0u;
    if (s->atk_base > (u8)MKD_SAVE_BOUND_ATK_MAX) return 0u;
    if (s->hp_max > (u8)MKD_SAVE_BOUND_HP_MAX) return 0u;
    if (s->crit_chance > (u8)MKD_SAVE_BOUND_CRIT_MAX) return 0u;
    if (s->checksum != mkd_save_checksum_compute(s)) return 0u;
    return 1u;
}

/* Initialise s_mkd_save avec des valeurs par defaut (pas de save existante).
 * Pattern shmup_profile.c save_defaults_set : init champ par champ + padding
 * loop, surtout PAS de memset/clear bytewise. */
static void mkd_save_defaults_set(void)
{
    u16 i;

    s_mkd_save.magic[0] = MKD_SAVE_MAGIC_0;
    s_mkd_save.magic[1] = MKD_SAVE_MAGIC_1;
    s_mkd_save.magic[2] = MKD_SAVE_MAGIC_2;
    s_mkd_save.magic[3] = MKD_SAVE_MAGIC_3;
    s_mkd_save.version = MKD_SAVE_VERSION;
    s_mkd_save.unlocked_levels = 0x01u;   /* slot 0 only */
    s_mkd_save.atk_base = 0u;             /* mis a jour par commit */
    s_mkd_save.hp_max = 0u;
    s_mkd_save.crit_chance = 0u;
    s_mkd_save.best_run_kills = 0u;
    s_mkd_save.best_run_floor = 0u;
    s_mkd_save.last_seed = 0u;

    for (i = 0u; i < (u16)sizeof(s_mkd_save.padding); i++) {
        s_mkd_save.padding[i] = 0u;
    }
    s_mkd_save.checksum = 0u;  /* sera recalcule au prochain commit */
}

/* Init au boot : tente load+validate ; sinon defaults. Si save valide,
 * applique sur g_player + s_level_select_unlocked. Pattern shmup_profile_init. */
static void mkd_save_init(void)
{
    u8 i;

    ngpc_flash_init();

    if (ngpc_flash_exists()) {
        ngpc_flash_load(&s_mkd_save);
        if (mkd_save_is_valid(&s_mkd_save)) {
            /* Apply sur RAM runtime. */
            for (i = 0u; i < 4u; i++) {
                s_level_select_unlocked[i] =
                    (u8)((s_mkd_save.unlocked_levels >> i) & 1u);
            }
            if (!s_level_select_unlocked[0]) s_level_select_unlocked[0] = 1u;

            g_player.atk_base    = s_mkd_save.atk_base;
            g_player.hp_max      = s_mkd_save.hp_max;
            g_player.hp          = s_mkd_save.hp_max;
            g_player.crit_chance = s_mkd_save.crit_chance;
            return;
        }
    }

    /* Pas de save valide -> defaults en RAM. Pas de flash write au boot
     * (cf STORAGE.md §5.4 : write at boot = power-off risk). */
    mkd_save_defaults_set();
}

/* Commit l'etat RAM (g_player + s_level_select_unlocked + g_run_*) vers flash.
 * Pattern shmup_profile.c save_commit : pas de load prealable, on packe direct
 * depuis l'etat RAM courant (s_mkd_save garde les records via le boot load
 * ou les commits precedents). Records best_run_* mis a jour avec max(). */
static void mkd_save_commit(void)
{
    u8 i;

    /* Garantit magic+version meme si s_mkd_save jamais init (safety). */
    s_mkd_save.magic[0] = MKD_SAVE_MAGIC_0;
    s_mkd_save.magic[1] = MKD_SAVE_MAGIC_1;
    s_mkd_save.magic[2] = MKD_SAVE_MAGIC_2;
    s_mkd_save.magic[3] = MKD_SAVE_MAGIC_3;
    s_mkd_save.version = MKD_SAVE_VERSION;

    /* Pack unlocks bitmask. */
    s_mkd_save.unlocked_levels = 0u;
    for (i = 0u; i < 4u; i++) {
        if (s_level_select_unlocked[i]) {
            s_mkd_save.unlocked_levels |= (u8)(1u << i);
        }
    }

    /* Stats actuelles de g_player (le caller ajuste l'etat RAM avant
     * d'appeler commit : victory garde bonus, game_over reset aux bases). */
    s_mkd_save.atk_base    = g_player.atk_base;
    s_mkd_save.hp_max      = g_player.hp_max;
    s_mkd_save.crit_chance = g_player.crit_chance;

    /* Records best (max avec l'existant). */
    if (g_run_kills > s_mkd_save.best_run_kills) {
        s_mkd_save.best_run_kills = g_run_kills;
    }
    if (g_depth_current > s_mkd_save.best_run_floor) {
        s_mkd_save.best_run_floor = g_depth_current;
    }
    s_mkd_save.last_seed = g_dungeon_seed;

    /* Checksum DOIT etre recalc en dernier (apres tous les fields). */
    s_mkd_save.checksum = mkd_save_checksum_compute(&s_mkd_save);

    ngpc_flash_save(&s_mkd_save);
}
static StaticRoomId s_static_room = STATIC_ROOM_01;
static StaticRoomEntry s_static_room_entry = ROOM_ENTRY_DEFAULT;
static const RoomCollision s_room_collisions[STATIC_ROOM_COUNT] = {
    { g_salle_01_col_map, SALLE_01_COL_MAP_W, SALLE_01_COL_MAP_H },
    { g_salle_02_col_map, SALLE_02_COL_MAP_W, SALLE_02_COL_MAP_H }
};

static const u8 s_level_select_pointer_pos_x[4] = {
    14u, 54u, 94u, 134u
};

static const u8 s_level_select_pointer_pos_y[4] = {
    106u, 106u, 106u, 106u
};

static const u8 s_level_select_lock_pos_x[4] = {
    0u, 53u, 93u, 133u
};

static const u8 s_level_select_lock_pos_y[4] = {
    0u, 94u, 94u, 94u
};

static const NgpcMetasprite * const s_player_base_frames[12] = {
    &player_topdown_frame_0,
    &player_topdown_frame_1,
    &player_topdown_frame_2,
    &player_topdown_frame_3,
    &player_topdown_frame_4,
    &player_topdown_frame_5,
    &player_topdown_frame_6,
    &player_topdown_frame_7,
    &player_topdown_frame_8,
    &player_topdown_frame_9,
    &player_topdown_frame_10,
    &player_topdown_frame_11
};

static const NgpcMetasprite * const s_player_overlay_frames[12] = {
    &player_topdown_layer1_frame_0,
    &player_topdown_layer1_frame_1,
    &player_topdown_layer1_frame_2,
    &player_topdown_layer1_frame_3,
    &player_topdown_layer1_frame_4,
    &player_topdown_layer1_frame_5,
    &player_topdown_layer1_frame_6,
    &player_topdown_layer1_frame_7,
    &player_topdown_layer1_frame_8,
    &player_topdown_layer1_frame_9,
    &player_topdown_layer1_frame_10,
    &player_topdown_layer1_frame_11
};

static const NgpcMetasprite * const s_enemy_frames[6] = {
    &lime_sheet_frame_0,
    &lime_sheet_frame_1,
    &lime_sheet_frame_2,
    &lime_sheet_frame_3,
    &lime_sheet_frame_4,
    &lime_sheet_frame_5
};

static void room_draw_actors(void);
static void hud_load_vram(void);
static void hud_draw(void);
static void room_begin_enemy_turns(void);

/* ---- State: Intro ---- */

#define INTRO_TILE_BASE 128u
#define MENU_SELECT_TILE_BASE 1u
#define MENU_SELECT_PAL 15u
#define MENU_SPRITE_TILE_COUNT 11u
/* Keep > NGPC_FONT_TILE_BASE + NGPC_FONT_TILE_COUNT (32+96=128) so room tiles
 * don't overwrite the custom font in VRAM slots 32-127. */
#define LEVEL_SELECT_TILE_BASE 200u
#define LEVEL_SELECT_POINTER_SPR_BASE 0u
#define LEVEL_SELECT_LOCK_SPR_BASE 4u
#define LEVEL_SELECT_LOCK_SPR_STRIDE 4u
#define LEVEL_SELECT_ENTRY_COUNT 4u
#define ROOM_TILE_BASE 200u
/* Pause menu — tiles overwrite the room's sprite region in VRAM while paused;
 * room_resume() reloads everything on exit. Cursor tiles must sit OUTSIDE
 * the pause tilemap VRAM range (128..233) — otherwise loading them corrupts
 * a pause bg tile and produces a ghost cursor on the screen. */
#define PAUSE_TILE_BASE 128u
#define PAUSE_CURSOR_SPR_BASE 0u
#define PAUSE_CURSOR_ANIM_TICKS 15u
#define ATTACK_EFFECT_SPR_BASE 0u
#define SELECTOR_SPR_BASE 4u
#define PLAYER_OVERLAY_SPR_BASE 8u
#define PLAYER_SPR_BASE 12u
#define ENEMY_SPR_BASE 16u
/* HUD info-only sprites : juste le texte "PV: NN" en bas-gauche (6 OAM).
 * On a abandonne le bandeau decoratif 4-metasprites (40 OAM) qui ne
 * portait aucune info dynamique -- trop cher pour un visuel pur. La
 * plage OAM 24..63 est maintenant libre pour de futurs elements UI.
 * Tiles font ahchay deja en VRAM (slot ASCII direct, 32..127). */
#define HP_TEXT_SPR_BASE 24u
#define HP_TEXT_X        2
#define HP_TEXT_Y        140  /* SCREEN_H - 12, base bas-gauche */
/* Slot SPR palette pour la font. Slots 0..12 reserves par les enemies
 * et skull_death (cf *_pal_base dans GraphX). 13 libre. */
#define FONT_SPR_PAL     14u
#define HUD_Y            (SCREEN_H - 16)  /* 136 -- toujours utilise pour reserver la zone bas */
#define ROOM_LINK_GX 4u
#define PLAYER_GRID_SIZE 16u
#define PLAYER_START_GX 5u
#define PLAYER_START_GY 6u
#define ENEMY_START_GX 8u
#define ENEMY_START_GY 6u
/* PLAYER_MAX_HP retire : utiliser g_player.hp_max (player_state.h). */
#define ENEMY_MAX_HP 3u
#define PLAYER_ATTACK_POWER 2u
#define ENEMY_ATTACK_POWER 1u
#define COMBAT_CRIT_ROLL_MIN 5u
#define COMBAT_ROLL_MAX 6u

/* Enemy types : EnemyType + EnemyStats defs en ROM dans game_stats.h.
 * Table g_enemy_stats[ENEMY_TYPE_COUNT] dans game_stats.c. */
#define PLAYER_MOVE_ANIM_FRAMES 8u
#define PLAYER_MOVE_STEP 2
#define PLAYER_IDLE_FRAME 1u
#define ENEMY_MOVE_ANIM_FRAMES 8u
#define ENEMY_MOVE_STEP 2
#define ENEMY_IDLE_FRAME 1u
#define ATTACK_EFFECT_VISUAL_NORMAL 0u
#define ATTACK_EFFECT_VISUAL_CRIT 1u
#define ATTACK_EFFECT_FRAME_TICKS 6u
#define DAMAGE_POPUP_FRAMES 50u
#define AFTER_PLAYER_ATTACK_DELAY_FRAMES 8u
#define ROOM_GRID_W ((u8)(SALLE_01_COL_MAP_W / 2u))
#define ROOM_GRID_H ((u8)(SALLE_01_COL_MAP_H / 2u))
#define ROOM_LINK_SOUTH_GY ((u8)(ROOM_GRID_H - 1u))
#define ROOM_ENTRY_NORTH_GY 1u
#define ROOM_ENTRY_SOUTH_GY ((u8)(ROOM_GRID_H - 2u))
#define ROOM2_STAIR_GX 4u
#define ROOM2_STAIR_GY 4u

static u16 menu_tile_base(void)
{
    return (u16)(TILE_MAX - MENU_SPRITE_TILE_COUNT -
        (u16)(menu_kuroi_dokutsu_tiles_count / TILE_WORDS));
}

static u16 menu_select_start_tile_base(void)
{
    return MENU_SELECT_TILE_BASE;
}

static u16 menu_select_continue_tile_base(void)
{
    return (u16)(menu_select_start_tile_base() +
        (u16)(menu_select_start_tiles_count / TILE_WORDS));
}

static u16 menu_select_options_tile_base(void)
{
    return (u16)(menu_select_continue_tile_base() +
        (u16)(menu_select_continue_tiles_count / TILE_WORDS));
}

static const RoomCollision *room_collision_data(void)
{
    return &s_room_collisions[(u8)s_static_room];
}

static u8 room_col_type(u8 tx, u8 ty)
{
    const RoomCollision *room;

    room = room_collision_data();

    if (tx >= room->map_w || ty >= room->map_h)
        return COL_SOLID;

    return room->map[(u16)ty * room->map_w + tx];
}

static u8 room_col_blocks(u8 type)
{
    return (type == COL_SOLID) ? 1u : 0u;
}

static s16 player_grid_to_screen(u8 g)
{
    return (s16)((u16)g * (u16)PLAYER_GRID_SIZE);
}

static u8 room_grid_cell_blocks(s8 gx, s8 gy)
{
    u8 tx;
    u8 ty;

    if (gx < 0 || gy < 0)
        return 1u;
    if (gx >= (s8)ROOM_GRID_W || gy >= (s8)ROOM_GRID_H)
        return 1u;

    tx = (u8)((u8)gx * 2u);
    ty = (u8)((u8)gy * 2u);

    if (room_col_blocks(room_col_type(tx, ty)))
        return 1u;
    if (room_col_blocks(room_col_type((u8)(tx + 1u), ty)))
        return 1u;
    if (room_col_blocks(room_col_type(tx, (u8)(ty + 1u))))
        return 1u;
    if (room_col_blocks(room_col_type((u8)(tx + 1u), (u8)(ty + 1u))))
        return 1u;

    return 0u;
}

static u8 player_at_grid(s8 gx, s8 gy)
{
    return (gx == (s8)s_player_gx && gy == (s8)s_player_gy) ? 1u : 0u;
}

static u8 enemy_at_grid(s8 gx, s8 gy)
{
    if (!s_enemy_active)
        return 0u;

    return (gx == (s8)s_enemy_gx && gy == (s8)s_enemy_gy) ? 1u : 0u;
}

static u8 room_action_target_at(s8 gx, s8 gy)
{
    return enemy_at_grid(gx, gy);
}

static u8 enemy_can_enter(s8 gx, s8 gy)
{
    if (room_grid_cell_blocks(gx, gy))
        return 0u;
    if (player_at_grid(gx, gy))
        return 0u;

    return 1u;
}

static u8 player_frame_index(void)
{
    return (u8)((u8)s_player_dir * 3u + s_player_anim_frame);
}

static void player_load_palettes(void)
{
    u8 i;
    u16 src;

    for (i = 0; i < player_topdown_palette_count; i++) {
        src = (u16)i * 4u;
        ngpc_gfx_set_palette(GFX_SPR, (u8)(player_topdown_pal_base + i),
            player_topdown_palettes[src],
            player_topdown_palettes[(u16)(src + 1u)],
            player_topdown_palettes[(u16)(src + 2u)],
            player_topdown_palettes[(u16)(src + 3u)]);
    }
}

static void enemy_load_palettes(void)
{
    u8 i;
    u16 src;

    for (i = 0; i < lime_sheet_palette_count; i++) {
        src = (u16)i * 4u;
        ngpc_gfx_set_palette(GFX_SPR, (u8)(lime_sheet_pal_base + i),
            lime_sheet_palettes[src],
            lime_sheet_palettes[(u16)(src + 1u)],
            lime_sheet_palettes[(u16)(src + 2u)],
            lime_sheet_palettes[(u16)(src + 3u)]);
    }
}

static void skull_load_palettes(void)
{
    u8 i;
    u16 src;

    for (i = 0; i < skull_palette_count; i++) {
        src = (u16)i * 4u;
        ngpc_gfx_set_palette(GFX_SPR, (u8)(skull_pal_base + i),
            skull_palettes[src],
            skull_palettes[(u16)(src + 1u)],
            skull_palettes[(u16)(src + 2u)],
            skull_palettes[(u16)(src + 3u)]);
    }
}

static void skull_death_load_palettes(void)
{
    u8 i;
    u16 src;

    for (i = 0; i < skull_death_palette_count; i++) {
        src = (u16)i * 4u;
        ngpc_gfx_set_palette(GFX_SPR, (u8)(skull_death_pal_base + i),
            skull_death_palettes[src],
            skull_death_palettes[(u16)(src + 1u)],
            skull_death_palettes[(u16)(src + 2u)],
            skull_death_palettes[(u16)(src + 3u)]);
    }
}

static void flamme_load_palettes(void)
{
    u8 i;
    u16 src;

    for (i = 0; i < flamme_palette_count; i++) {
        src = (u16)i * 4u;
        ngpc_gfx_set_palette(GFX_SPR, (u8)(flamme_pal_base + i),
            flamme_palettes[src],
            flamme_palettes[(u16)(src + 1u)],
            flamme_palettes[(u16)(src + 2u)],
            flamme_palettes[(u16)(src + 3u)]);
    }
}

static void hent_load_palettes(void)
{
    u8 i;
    u16 src;

    for (i = 0; i < hent_palette_count; i++) {
        src = (u16)i * 4u;
        ngpc_gfx_set_palette(GFX_SPR, (u8)(hent_pal_base + i),
            hent_palettes[src],
            hent_palettes[(u16)(src + 1u)],
            hent_palettes[(u16)(src + 2u)],
            hent_palettes[(u16)(src + 3u)]);
    }
}

static void selector_load_palettes(void)
{
    u8 i;
    u16 src;

    for (i = 0; i < selecteur_palette_count; i++) {
        src = (u16)i * 4u;
        ngpc_gfx_set_palette(GFX_SPR, (u8)(selecteur_pal_base + i),
            selecteur_palettes[src],
            selecteur_palettes[(u16)(src + 1u)],
            selecteur_palettes[(u16)(src + 2u)],
            selecteur_palettes[(u16)(src + 3u)]);
    }
}

static void attack_effect_load_palettes(void)
{
    u8 i;
    u16 src;

    for (i = 0; i < effect_attaque_palette_count; i++) {
        src = (u16)i * 4u;
        ngpc_gfx_set_palette(GFX_SPR, (u8)(effect_attaque_pal_base + i),
            effect_attaque_palettes[src],
            effect_attaque_palettes[(u16)(src + 1u)],
            effect_attaque_palettes[(u16)(src + 2u)],
            effect_attaque_palettes[(u16)(src + 3u)]);
    }
}

static void attack_effect_crit_load_palettes(void)
{
    u8 i;
    u16 src;

    for (i = 0; i < effect_attaque_crit_palette_count; i++) {
        src = (u16)i * 4u;
        ngpc_gfx_set_palette(GFX_SPR, (u8)(effect_attaque_crit_pal_base + i),
            effect_attaque_crit_palettes[src],
            effect_attaque_crit_palettes[(u16)(src + 1u)],
            effect_attaque_crit_palettes[(u16)(src + 2u)],
            effect_attaque_crit_palettes[(u16)(src + 3u)]);
    }
}

static void level_select_pointer_load_palettes(void)
{
    u8 i;
    u16 src;

    for (i = 0; i < menu_level_pointer_palette_count; i++) {
        src = (u16)i * 4u;
        ngpc_gfx_set_palette(GFX_SPR, (u8)(menu_level_pointer_pal_base + i),
            menu_level_pointer_palettes[src],
            menu_level_pointer_palettes[(u16)(src + 1u)],
            menu_level_pointer_palettes[(u16)(src + 2u)],
            menu_level_pointer_palettes[(u16)(src + 3u)]);
    }
}

static void level_select_lock_load_palettes(void)
{
    u8 i;
    u16 src;

    for (i = 0; i < menu_level_lock_palette_count; i++) {
        src = (u16)i * 4u;
        ngpc_gfx_set_palette(GFX_SPR, (u8)(menu_level_lock_pal_base + i),
            menu_level_lock_palettes[src],
            menu_level_lock_palettes[(u16)(src + 1u)],
            menu_level_lock_palettes[(u16)(src + 2u)],
            menu_level_lock_palettes[(u16)(src + 3u)]);
    }
}

static void level_select_pointer_update_position(void)
{
    s_level_select_pointer_x = s_level_select_pointer_pos_x[s_level_select_index];
    s_level_select_pointer_y = s_level_select_pointer_pos_y[s_level_select_index];
}

static u8 level_select_is_unlocked(u8 index)
{
    if (index >= LEVEL_SELECT_ENTRY_COUNT)
        return 0u;

    return s_level_select_unlocked[index];
}

static void level_select_draw_pointer(void)
{
    ngpc_mspr_draw((u8)LEVEL_SELECT_POINTER_SPR_BASE,
        s_level_select_pointer_x,
        s_level_select_pointer_y,
        &menu_level_pointer_frame_0,
        (u8)SPR_FRONT);
}

static void level_select_draw_locks(void)
{
    u8 i;
    u8 spr;

    for (i = 1u; i < LEVEL_SELECT_ENTRY_COUNT; i++) {
        spr = (u8)(LEVEL_SELECT_LOCK_SPR_BASE +
            (u8)((u8)(i - 1u) * LEVEL_SELECT_LOCK_SPR_STRIDE));

        if (level_select_is_unlocked(i)) {
            ngpc_mspr_hide(spr, LEVEL_SELECT_LOCK_SPR_STRIDE);
            continue;
        }

        ngpc_mspr_draw(spr,
            s_level_select_lock_pos_x[i],
            s_level_select_lock_pos_y[i],
            &menu_level_lock_frame_0,
            (u8)SPR_FRONT);
    }
}

static void level_select_draw_ui(void)
{
    level_select_draw_pointer();
    level_select_draw_locks();
}

static void player_reset(void)
{
    s_room_turn_state = ROOM_TURN_WAIT_INPUT;
    s_turn_action = TURN_ACTION_NONE;
    s_after_player_attack_delay = 0u;
    s_player_gx = PLAYER_START_GX;
    s_player_gy = PLAYER_START_GY;
    s_player_target_gx = PLAYER_START_GX;
    s_player_target_gy = PLAYER_START_GY;
    s_player_x = player_grid_to_screen(PLAYER_START_GX);
    s_player_y = player_grid_to_screen(PLAYER_START_GY);
    s_player_step_x = 0;
    s_player_step_y = 0;
    s_player_dir = PLAYER_DIR_DOWN;
    s_player_anim_frame = PLAYER_IDLE_FRAME;
    s_player_anim_timer = 0u;
    g_player.hp = g_player.hp_max;
}

static void enemy_reset(void)
{
    s_enemy_active = 1u;
    s_enemy_hp = ENEMY_MAX_HP;
    s_enemy_dir = PLAYER_DIR_LEFT;
    s_enemy_gx = ENEMY_START_GX;
    s_enemy_gy = ENEMY_START_GY;
    s_enemy_target_gx = ENEMY_START_GX;
    s_enemy_target_gy = ENEMY_START_GY;
    s_enemy_x = player_grid_to_screen(ENEMY_START_GX);
    s_enemy_y = player_grid_to_screen(ENEMY_START_GY);
    s_enemy_step_x = 0;
    s_enemy_step_y = 0;
    s_enemy_anim_frame = ENEMY_IDLE_FRAME;
    s_enemy_anim_timer = 0u;
}

static void player_place_for_room_entry(u8 keep_hp)
{
    u8 hp;
    u8 gx;
    u8 gy;
    PlayerDirection dir;

    hp = g_player.hp;
    gx = PLAYER_START_GX;
    gy = PLAYER_START_GY;
    dir = PLAYER_DIR_DOWN;

    player_reset();
    if (keep_hp) {
        g_player.hp = hp;
    }

    if (s_static_room == STATIC_ROOM_01) {
        if (s_static_room_entry == ROOM_ENTRY_FROM_NORTH) {
            gx = ROOM_LINK_GX;
            gy = ROOM_ENTRY_NORTH_GY;
            dir = PLAYER_DIR_DOWN;
        }
    } else {
        if (s_static_room_entry == ROOM_ENTRY_FROM_SOUTH) {
            gx = ROOM_LINK_GX;
            gy = ROOM_ENTRY_SOUTH_GY;
            dir = PLAYER_DIR_UP;
        } else if (s_static_room_entry == ROOM_ENTRY_FROM_STAIR) {
            gx = ROOM2_STAIR_GX;
            gy = (u8)(ROOM2_STAIR_GY + 1u);
            dir = PLAYER_DIR_DOWN;
        } else {
            gx = ROOM_LINK_GX;
            gy = ROOM_ENTRY_SOUTH_GY;
            dir = PLAYER_DIR_UP;
        }
    }

    s_player_gx = gx;
    s_player_gy = gy;
    s_player_target_gx = gx;
    s_player_target_gy = gy;
    s_player_x = player_grid_to_screen(gx);
    s_player_y = player_grid_to_screen(gy);
    s_player_dir = dir;
    s_player_anim_frame = PLAYER_IDLE_FRAME;
}

static void enemy_place_for_current_room(void)
{
    enemy_reset();

    if (s_static_room == STATIC_ROOM_02) {
        s_enemy_dir = PLAYER_DIR_LEFT;
        s_enemy_gx = 7u;
        s_enemy_gy = 6u;
        s_enemy_target_gx = 7u;
        s_enemy_target_gy = 6u;
        s_enemy_x = player_grid_to_screen(7u);
        s_enemy_y = player_grid_to_screen(6u);
    }
}

static void room_enter(StaticRoomId room_id, StaticRoomEntry entry, u8 keep_hp);

static void player_face(PlayerDirection dir)
{
    s_player_dir = dir;
    s_player_anim_frame = PLAYER_IDLE_FRAME;
    s_player_anim_timer = 0u;
}

static void player_set_walk_frame(void)
{
    u8 phase;

    phase = (u8)((s_player_anim_timer >> 1) & 0x03u);
    switch (phase) {
    case 0u:
        s_player_anim_frame = 0u;
        break;
    case 1u:
    case 3u:
        s_player_anim_frame = PLAYER_IDLE_FRAME;
        break;
    default:
        s_player_anim_frame = 2u;
        break;
    }
}

static void player_draw(void)
{
    u8 frame;

    frame = player_frame_index();
    ngpc_mspr_draw((u8)PLAYER_SPR_BASE, s_player_x, s_player_y,
        (const NgpcMetasprite *)s_player_base_frames[frame], (u8)SPR_FRONT);
    ngpc_mspr_draw((u8)PLAYER_OVERLAY_SPR_BASE, s_player_x, s_player_y,
        (const NgpcMetasprite *)s_player_overlay_frames[frame], (u8)SPR_FRONT);
}

static u8 enemy_frame_index(void)
{
    switch (s_enemy_dir) {
    case PLAYER_DIR_UP:
    case PLAYER_DIR_DOWN:
        return (u8)(3u + s_enemy_anim_frame);
    case PLAYER_DIR_LEFT:
    case PLAYER_DIR_RIGHT:
    default:
        return s_enemy_anim_frame;
    }
}

static u8 enemy_draw_flags(void)
{
    switch (s_enemy_dir) {
    case PLAYER_DIR_LEFT:
        return (u8)(SPR_FRONT | SPR_HFLIP);
    case PLAYER_DIR_DOWN:
        return (u8)(SPR_FRONT | SPR_VFLIP);
    case PLAYER_DIR_UP:
    case PLAYER_DIR_RIGHT:
    default:
        return (u8)SPR_FRONT;
    }
}

static void enemy_draw(void)
{
    u8 frame;

    if (!s_enemy_active) {
        ngpc_mspr_hide((u8)ENEMY_SPR_BASE, 4u);
        return;
    }

    frame = enemy_frame_index();
    ngpc_mspr_draw((u8)ENEMY_SPR_BASE, s_enemy_x, s_enemy_y,
        (const NgpcMetasprite *)s_enemy_frames[frame], enemy_draw_flags());
}

static void selector_hide(void)
{
    s_selector_visible = 0u;
    ngpc_mspr_hide((u8)SELECTOR_SPR_BASE, 4u);
}

static void selector_show(u8 gx, u8 gy)
{
    s_selector_visible = 1u;
    s_selector_gx = gx;
    s_selector_gy = gy;
}

static void selector_draw(void)
{
    if (!s_selector_visible) {
        ngpc_mspr_hide((u8)SELECTOR_SPR_BASE, 4u);
        return;
    }

    ngpc_mspr_draw((u8)SELECTOR_SPR_BASE,
        player_grid_to_screen(s_selector_gx),
        player_grid_to_screen(s_selector_gy),
        &selecteur_frame_0,
        (u8)SPR_FRONT);
}

static u8 damage_popup_tile_x(u8 gx)
{
    u8 tx;

    tx = (u8)(gx * 2u);
    if (tx >= (u8)(SCREEN_TW - 1u))
        tx = (u8)(SCREEN_TW - 2u);

    return tx;
}

static u8 damage_popup_tile_y(u8 gy)
{
    u8 ty;

    ty = (u8)(gy * 2u);
    if (ty > 0u)
        ty = (u8)(ty - 1u);

    return ty;
}

static void damage_popup_erase_at(u8 gx, u8 gy)
{
    ngpc_text_print(GFX_SCR2, 0u,
        damage_popup_tile_x(gx),
        damage_popup_tile_y(gy),
        "  ");
}

static void damage_popup_hide(void)
{
    if (s_damage_popup_visible) {
        if (s_damage_popup_is_fatal) {
            ngpc_mspr_hide((u8)SKULL_DEATH_SPR_BASE, 4u);
        } else {
            damage_popup_erase_at(s_damage_popup_gx, s_damage_popup_gy);
        }
    }

    s_damage_popup_visible = 0u;
    s_damage_popup_value = 0u;
    s_damage_popup_timer = 0u;
    s_damage_popup_source = ATTACK_EFFECT_NONE;
    s_damage_popup_is_fatal = 0u;
}

static void damage_popup_draw(void)
{
    if (!s_damage_popup_visible)
        return;

    if (s_damage_popup_is_fatal) {
        /* Skull 16x16 au-dessus du joueur (au lieu du nombre de degats).
         * Position pixel : on suit le sprite player (s_player_x/y deja
         * en coord ecran absolue). */
        ngpc_mspr_draw((u8)SKULL_DEATH_SPR_BASE,
            s_player_x, (s16)(s_player_y - 16),
            &skull_death_frame_0, (u8)SPR_FRONT);
        return;
    }

    ngpc_text_print_num(GFX_SCR2, 0u,
        damage_popup_tile_x(s_damage_popup_gx),
        damage_popup_tile_y(s_damage_popup_gy),
        s_damage_popup_value,
        2u);
}

static void damage_popup_start(u8 gx, u8 gy, u8 value, AttackEffectSource source)
{
    s_damage_popup_visible = 1u;
    s_damage_popup_gx = gx;
    s_damage_popup_gy = gy;
    s_damage_popup_value = value;
    s_damage_popup_timer = DAMAGE_POPUP_FRAMES;
    s_damage_popup_source = source;
    /* Detect hit fatal : enemy a tue le joueur (HP arrive a 0). */
    s_damage_popup_is_fatal = (u8)
        ((source == ATTACK_EFFECT_ENEMY && g_player.hp == 0u) ? 1u : 0u);
}

static void attack_effect_hide(void)
{
    s_attack_effect_visible = 0u;
    s_attack_effect_visual = ATTACK_EFFECT_VISUAL_NORMAL;
    s_attack_effect_source = ATTACK_EFFECT_NONE;
    ngpc_mspr_hide((u8)ATTACK_EFFECT_SPR_BASE, 4u);
}

static void attack_effect_draw(void)
{
    const NgpcMetasprite *frame;
    u8 flags;

    if (!s_attack_effect_visible) {
        ngpc_mspr_hide((u8)ATTACK_EFFECT_SPR_BASE, 4u);
        return;
    }

    frame = &effect_attaque_frame_0;
    if (s_attack_effect_visual == ATTACK_EFFECT_VISUAL_CRIT) {
        frame = &effect_attaque_crit_frame_0;
    }

    flags = (u8)SPR_FRONT;
    if (s_attack_effect_timer >= ATTACK_EFFECT_FRAME_TICKS) {
        flags = (u8)(flags | SPR_HFLIP);
    }

    ngpc_mspr_draw((u8)ATTACK_EFFECT_SPR_BASE,
        player_grid_to_screen(s_attack_effect_gx),
        player_grid_to_screen(s_attack_effect_gy),
        frame,
        flags);
}

/* HUD bandeau bas via raster split SCR2 (cf ngpc_raster K2GE doc) :
 * - Tiles "PV: NN" ecrits dans le tilemap SCR2 a tile_y = HUD_BG_TILE_Y (17).
 * - scroll table SCR2 met scroll_y = 0 sur lignes >= HUD_BG_SPLIT_LINE,
 *   = cam_y au-dessus -> la zone bas (16 px) montre toujours tile_y=17/18.
 * Aucun OAM consomme par le HUD texte. ngpc_raster_init() au boot + le
 * pointer table installe une seule fois. */
/* Installe la palette font ahchay sur le slot SPR FONT_SPR_PAL (utilisee
 * par les sprites Press A en game over et skull_death popup mort). */
static void install_font_palette_spr(void)
{
    static const u16 NGP_FAR pal[4] = {
        /* C0 = transparent en SPR (toujours), C1 = blanc, C2/C3 = noir. */
        0x0000, 0x0FFF, 0x0000, 0x0000,
    };
    u16 src = 0u;
    ngpc_gfx_set_palette(GFX_SPR, (u8)FONT_SPR_PAL,
        pal[src], pal[(u16)(src + 1u)],
        pal[(u16)(src + 2u)], pal[(u16)(src + 3u)]);
}

/* Charge en VRAM : tiles du bandeau HUD.png a HUD_BG_TILE_BASE, palette
 * du bandeau sur SCR2 slot HUD_BG_PAL_SCR2. Aussi (re)installe la
 * palette font sur SPR pour les sprites texte (Press A, etc.). */
static void hud_load_vram(void)
{
    u16 i;
    /* Tiles bandeau HUD : 9 tiles uniques, 72 u16 words. */
    NGP_TILEMAP_LOAD_TILES_VRAM(hud_bg, HUD_BG_TILE_BASE);

    /* Palette bandeau sur SCR2 slot HUD_BG_PAL_SCR2. hud_bg_palette_count=1
     * -> charge slot HUD_BG_PAL_SCR2 uniquement. Pattern array+loop runtime
     * pour minimiser le risque cc900 (i runtime, pas constant-folded). */
    for (i = 0u; i < (u16)hud_bg_palette_count; i++) {
        u16 src = (u16)i * 4u;
        ngpc_gfx_set_palette(GFX_SCR2,
            (u8)((u8)HUD_BG_PAL_SCR2 + (u8)i),
            hud_bg_palettes[src],
            hud_bg_palettes[(u16)(src + 1u)],
            hud_bg_palettes[(u16)(src + 2u)],
            hud_bg_palettes[(u16)(src + 3u)]);
    }

    install_font_palette_spr();
}

/* Blitte le tilemap HUD.png (20x2 tiles) au tilemap SCR2 a (0, HUD_BG_TILE_Y).
 * Puis ecrit "PV:" par-dessus en pal 0 (font ahchay) a tile_x=0..2. Le
 * chiffre lui est rafraichi par hud_draw() chaque frame. */
static void hud_paint_bg(void)
{
    u16 i;
    for (i = 0u; i < (u16)hud_bg_map_len; i++) {
        u8 x = (u8)(i % (u16)hud_bg_map_w);
        u8 y = (u8)(i / (u16)hud_bg_map_w);
        u16 off  = (u16)((u16)((u8)HUD_BG_TILE_Y + y) * 32u + (u16)x);
        u16 tile = (u16)((u16)HUD_BG_TILE_BASE + hud_bg_map_tiles[i]);
        u16 pal  = (u16)HUD_BG_PAL_SCR2;
        HW_SCR2_MAP[off] = (u16)(tile + (pal << 9));
    }
    /* Texte "PV:" sur la rangee du bas du HUD (row HUD_BG_TILE_Y+1).
     * Utilise hud_text_print (variant opaque : bg noir + letter blanc).
     * MKD-depth : le label "F:" et le chiffre sont ecrits par hud_draw
     * conditionnellement (depth>0 seulement) pour ne pas afficher de
     * floor stale en salle_01. */
    hud_text_print(GFX_SCR2, 0u, (u8)(HUD_BG_TILE_Y + 1u), "PV:");
}

/* Update du chiffre HP + depth a chaque frame. Ecrit sur la rangee
 * HUD_BG_TILE_Y+1 (bottom row du HUD).
 *
 * MKD-depth : le label "F:" + chiffre n'est ecrit que si depth > 0
 * (= en dungeon). En salle_01 ou apres game_over -> menu -> salle_01,
 * la zone est ecrasee par 4 espaces pour eviter d'afficher la profondeur
 * stale du run precedent. */
static void hud_draw(void)
{
    hud_text_print_num(GFX_SCR2, 3u, (u8)(HUD_BG_TILE_Y + 1u),
        g_player.hp, 2u);
    if (g_depth_current > 0u) {
        hud_text_print(GFX_SCR2, 6u, (u8)(HUD_BG_TILE_Y + 1u), "F:");
        hud_text_print_num(GFX_SCR2, 8u, (u8)(HUD_BG_TILE_Y + 1u),
            (u16)g_depth_current, 2u);
    } else {
        hud_text_print(GFX_SCR2, 6u, (u8)(HUD_BG_TILE_Y + 1u), "    ");
    }
}

/* MKD-8 v2 : la seed est affichee en bas-gauche du menu pause (continu) et
 * dans l'ecran Options (modifiable). L'ancien affichage HUD 2s a ete retire. */

/* Active le split HUD : le main loop (apres ngpc_vsync) appellera
 * hud_apply_scroll_and_arm() chaque frame tant que le flag reste a 1.
 * Cf bloc HUD split v4 autour de la ligne 320. */
static void hud_raster_update(void)
{
    s_dung_hud_split_armed = 1u;
}

/* Desactive le split HUD : pour transition vers menu/game_over/victory.
 * Stop Timer0 immediatement au cas ou il etait deja arme pour cette frame.
 *
 * Bug fix 2026-05-17 #1 : reset HW_SCR2_OFS_X/Y a (0, 0). Sinon la derniere
 * valeur posee par isr_hud_split (= (0, HUD_BG_SCROLL_Y=104)) reste dans
 * les registres K2GE -> au 2e lancement, AVANT que le main loop push les
 * nouvelles shadows, SCR2 affiche tile_y=30 (zone HUD) au lieu de tile_y=0.
 *
 * Bug fix 2026-05-17 #2 : reset les shadows scroll a (0, 0). Sinon dans une
 * room statique (salle_01/02) qui n'appelle PAS dungeon_update_camera, les
 * shadows gardent les valeurs cam_x/cam_y du dungeon precedent -> a la
 * 1ere frame visible le main loop hud_apply_scroll_and_arm() push ces vieilles
 * valeurs -> decor SCR1+SCR2 zone gameplay scrolle vers le mauvais endroit
 * (decale d'environ cam_y pixels) alors que les collisions restent correctes. */
static void hud_raster_disable(void)
{
    s_dung_hud_split_armed = 0u;
    HW_TRUN &= (u8)~0x01u;
    HW_SCR2_OFS_X = 0u;
    HW_SCR2_OFS_Y = 0u;
    s_dung_scroll_scr1_x = 0u;
    s_dung_scroll_scr1_y = 0u;
    s_dung_scroll_scr2_x = 0u;
    s_dung_scroll_scr2_y = 0u;
}

static void room_draw_actors(void)
{
    player_draw();
    enemy_draw();
    selector_draw();
    damage_popup_draw();
    attack_effect_draw();
    hud_draw();
}

static void attack_effect_start(u8 gx, u8 gy, u8 visual,
    AttackEffectSource source, u8 damage)
{
    s_attack_effect_visible = 1u;
    s_attack_effect_gx = gx;
    s_attack_effect_gy = gy;
    s_attack_effect_timer = 0u;
    s_attack_effect_visual = visual;
    s_attack_effect_source = source;
    damage_popup_start(gx, gy, damage, source);
    s_room_turn_state = ROOM_TURN_ATTACK_EFFECT;
    room_draw_actors();
}

static u8 combat_roll_damage(u8 attack_power, u8 *visual)
{
    u8 roll;
    u8 bonus;

    *visual = ATTACK_EFFECT_VISUAL_NORMAL;
    /* Use ngpc_qrandom (u8 table lookup) to avoid cc900 u32 arithmetic
     * in ngpc_random, which appears to bias rolls and blow up damage. */
    roll = (u8)(ngpc_qrandom() % (u8)(COMBAT_ROLL_MAX + 1u));

    if (roll == 0u)
        return 0u;

    if (attack_power == 0u)
        return 0u;

    bonus = (u8)(ngpc_qrandom() % attack_power);

    if (roll >= COMBAT_CRIT_ROLL_MIN) {
        *visual = ATTACK_EFFECT_VISUAL_CRIT;
        return (u8)(attack_power + 1u + bonus);
    }

    return (u8)(1u + bonus);
}

/* Dungeon enemies use per-type damage ranges (EnemyStats) instead of the
 * single attack_power model used for the salle_01 slime. */
static u8 enemy_roll_damage_typed(u8 type, u8 *visual)
{
    const EnemyStats *st;
    u8 roll;
    u8 base;
    u8 range;

    *visual = ATTACK_EFFECT_VISUAL_NORMAL;

    if (type >= (u8)ENEMY_TYPE_COUNT)
        return 0u;

    st = &g_enemy_stats[type];
    roll = (u8)(ngpc_qrandom() % (u8)(COMBAT_ROLL_MAX + 1u));

    if (roll == 0u)
        return 0u;

    if (roll >= COMBAT_CRIT_ROLL_MIN) {
        *visual = ATTACK_EFFECT_VISUAL_CRIT;
        base = st->crit_min;
        range = (u8)(st->crit_max - st->crit_min + 1u);
    } else {
        base = st->normal_min;
        range = (u8)(st->normal_max - st->normal_min + 1u);
    }

    if (range <= 1u)
        return base;
    return (u8)(base + (ngpc_qrandom() % range));
}

static void enemy_apply_damage(u8 damage)
{
    if (damage >= s_enemy_hp) {
        s_enemy_hp = 0u;
        return;
    }

    s_enemy_hp = (u8)(s_enemy_hp - damage);
}

static void player_apply_damage(u8 damage)
{
    /* Delegue le calcul HP au module player_state (centralisation stats).
     * Death sequence (anim + timer) reste owned par main.c. */
    player_state_apply_damage(damage);

    if (g_player.hp == 0u && s_death_phase == 0u) {
        s_death_phase = 1u;
        s_death_timer = DEATH_WAIT_FRAMES;
    }
}

static void player_attack_enemy(u8 target_gx, u8 target_gy)
{
    u8 visual;
    u8 damage;

    selector_hide();

    if (!enemy_at_grid((s8)target_gx, (s8)target_gy)) {
        room_draw_actors();
        return;
    }

    s_turn_action = TURN_ACTION_ATTACK;
    s_player_anim_frame = PLAYER_IDLE_FRAME;
    s_player_anim_timer = 0u;

    damage = combat_roll_damage(g_player.atk_base, &visual);
    if (damage > 0u)
        enemy_apply_damage(damage);

    /* Always play the attack effect + damage popup so a miss is visible
     * (popup shows 0). Otherwise the player can't tell their turn happened. */
    attack_effect_start(target_gx, target_gy, visual, ATTACK_EFFECT_PLAYER, damage);
}

static void enemy_attack_player(void)
{
    u8 visual;
    u8 damage;

    damage = combat_roll_damage(ENEMY_ATTACK_POWER, &visual);
    if (damage > 0u)
        player_apply_damage(damage);

    /* Always show the attack effect + popup, even on a miss (popup = 0). */
    attack_effect_start(s_player_gx, s_player_gy, visual, ATTACK_EFFECT_ENEMY, damage);
}

static void enemy_face(PlayerDirection dir)
{
    s_enemy_dir = dir;
    s_enemy_anim_frame = ENEMY_IDLE_FRAME;
    s_enemy_anim_timer = 0u;
}

static u8 grid_abs_diff(u8 a, u8 b)
{
    return (a > b) ? (u8)(a - b) : (u8)(b - a);
}

static s8 axis_step_toward(u8 from, u8 to)
{
    if (from < to)
        return 1;
    if (from > to)
        return -1;
    return 0;
}

static PlayerDirection dir_from_delta(s8 dx, s8 dy)
{
    if (dx < 0)
        return PLAYER_DIR_LEFT;
    if (dx > 0)
        return PLAYER_DIR_RIGHT;
    if (dy < 0)
        return PLAYER_DIR_UP;
    return PLAYER_DIR_DOWN;
}

static u8 enemy_adjacent_to_player(void)
{
    u8 dx;
    u8 dy;

    dx = grid_abs_diff(s_enemy_gx, s_player_gx);
    dy = grid_abs_diff(s_enemy_gy, s_player_gy);

    return ((u8)(dx + dy) == 1u) ? 1u : 0u;
}

static void enemy_face_player(void)
{
    s8 dx;
    s8 dy;

    dx = axis_step_toward(s_enemy_gx, s_player_gx);
    dy = axis_step_toward(s_enemy_gy, s_player_gy);

    if (grid_abs_diff(s_enemy_gx, s_player_gx) >=
        grid_abs_diff(s_enemy_gy, s_player_gy)) {
        if (dx != 0) {
            enemy_face(dir_from_delta(dx, 0));
            return;
        }
    }

    if (dy != 0) {
        enemy_face(dir_from_delta(0, dy));
    }
}

static void enemy_set_walk_frame(void)
{
    u8 phase;

    phase = (u8)((s_enemy_anim_timer >> 1) & 0x03u);
    switch (phase) {
    case 0u:
        s_enemy_anim_frame = 0u;
        break;
    case 1u:
    case 3u:
        s_enemy_anim_frame = ENEMY_IDLE_FRAME;
        break;
    default:
        s_enemy_anim_frame = 2u;
        break;
    }
}

static void enemy_start_move(s8 dx, s8 dy)
{
    enemy_face(dir_from_delta(dx, dy));
    s_enemy_target_gx = (u8)((s8)s_enemy_gx + dx);
    s_enemy_target_gy = (u8)((s8)s_enemy_gy + dy);
    s_enemy_step_x = (s8)(dx * ENEMY_MOVE_STEP);
    s_enemy_step_y = (s8)(dy * ENEMY_MOVE_STEP);
    s_enemy_anim_frame = 0u;
    s_enemy_anim_timer = 0u;
    s_room_turn_state = ROOM_TURN_ENEMY_ANIM;
    room_draw_actors();
}

static u8 enemy_try_move_axis(s8 dx, s8 dy)
{
    s8 nx;
    s8 ny;

    if (dx == 0 && dy == 0)
        return 0u;

    nx = (s8)((s8)s_enemy_gx + dx);
    ny = (s8)((s8)s_enemy_gy + dy);

    if (!enemy_can_enter(nx, ny))
        return 0u;

    enemy_start_move(dx, dy);
    return 1u;
}

/* ---- BFS pathfinding (enemy → player) ----
 * Grid 10x9 = 90 cells. s_bfs_first_dir holds, per cell, the direction of the
 * very first step taken from the enemy origin to reach that cell.
 *   0       = unvisited
 *   0xFFu   = enemy origin (no first step yet)
 *   1..4    = first step direction (UP/DOWN/LEFT/RIGHT)
 */
#define BFS_AREA  90u  /* ROOM_GRID_W (10) * ROOM_GRID_H (9) */
#define BFS_DIR_UP    1u
#define BFS_DIR_DOWN  2u
#define BFS_DIR_LEFT  3u
#define BFS_DIR_RIGHT 4u

static u8 s_bfs_first_dir[BFS_AREA];
static u8 s_bfs_qx[BFS_AREA];
static u8 s_bfs_qy[BFS_AREA];

static u8 bfs_idx(u8 gx, u8 gy)
{
    return (u8)((u8)(gy * ROOM_GRID_W) + gx);
}

static u8 enemy_bfs_first_dir(void)
{
    u8 head;
    u8 tail;
    u8 i;
    u8 cx;
    u8 cy;
    u8 cur_first;
    u8 d;
    s8 nx;
    s8 ny;
    u8 nidx;
    u8 nfirst;

    for (i = 0u; i < BFS_AREA; i++)
        s_bfs_first_dir[i] = 0u;

    s_bfs_first_dir[bfs_idx(s_enemy_gx, s_enemy_gy)] = 0xFFu;
    s_bfs_qx[0] = s_enemy_gx;
    s_bfs_qy[0] = s_enemy_gy;
    head = 0u;
    tail = 1u;

    while (head < tail) {
        cx = s_bfs_qx[head];
        cy = s_bfs_qy[head];
        head++;
        cur_first = s_bfs_first_dir[bfs_idx(cx, cy)];

        for (d = BFS_DIR_UP; d <= BFS_DIR_RIGHT; d++) {
            nx = (s8)cx;
            ny = (s8)cy;
            if (d == BFS_DIR_UP) {
                ny = (s8)(ny - 1);
            } else if (d == BFS_DIR_DOWN) {
                ny = (s8)(ny + 1);
            } else if (d == BFS_DIR_LEFT) {
                nx = (s8)(nx - 1);
            } else {
                nx = (s8)(nx + 1);
            }

            if (nx < 0 || ny < 0)
                continue;
            if (nx >= (s8)ROOM_GRID_W || ny >= (s8)ROOM_GRID_H)
                continue;

            nidx = bfs_idx((u8)nx, (u8)ny);
            if (s_bfs_first_dir[nidx] != 0u)
                continue;

            nfirst = (cur_first == 0xFFu) ? d : cur_first;

            /* Goal: reached the player's cell — return first step from enemy. */
            if ((u8)nx == s_player_gx && (u8)ny == s_player_gy)
                return nfirst;

            if (!enemy_can_enter(nx, ny))
                continue;

            s_bfs_first_dir[nidx] = nfirst;
            s_bfs_qx[tail] = (u8)nx;
            s_bfs_qy[tail] = (u8)ny;
            tail++;
        }
    }

    return 0u;
}

static u8 enemy_take_turn(void)
{
    u8 dir;

    if (!s_enemy_active)
        return 0u;

    enemy_face_player();

    if (enemy_adjacent_to_player()) {
        enemy_attack_player();
        return 1u;
    }

    dir = enemy_bfs_first_dir();
    if (dir == 0u)
        return 0u;

    if (dir == BFS_DIR_UP)
        return enemy_try_move_axis(0, -1);
    if (dir == BFS_DIR_DOWN)
        return enemy_try_move_axis(0, 1);
    if (dir == BFS_DIR_LEFT)
        return enemy_try_move_axis(-1, 0);
    return enemy_try_move_axis(1, 0);
}

static void enemy_update_move_anim(void)
{
    if (s_enemy_anim_timer < ENEMY_MOVE_ANIM_FRAMES) {
        s_enemy_x = (s16)(s_enemy_x + s_enemy_step_x);
        s_enemy_y = (s16)(s_enemy_y + s_enemy_step_y);
        s_enemy_anim_timer++;
        enemy_set_walk_frame();
    }

    if (s_enemy_anim_timer >= ENEMY_MOVE_ANIM_FRAMES) {
        s_enemy_gx = s_enemy_target_gx;
        s_enemy_gy = s_enemy_target_gy;
        s_enemy_x = player_grid_to_screen(s_enemy_gx);
        s_enemy_y = player_grid_to_screen(s_enemy_gy);
        s_enemy_step_x = 0;
        s_enemy_step_y = 0;
        s_enemy_anim_frame = ENEMY_IDLE_FRAME;
        s_turn_action = TURN_ACTION_NONE;
        s_room_turn_state = ROOM_TURN_WAIT_INPUT;
    }

    room_draw_actors();
}

static void room_begin_enemy_turns(void)
{
    selector_hide();
    s_room_turn_state = ROOM_TURN_ENEMY_TURNS;
}

static void room_run_enemy_turns(void)
{
    switch (s_turn_action) {
    case TURN_ACTION_MOVE:
    case TURN_ACTION_ATTACK:
    case TURN_ACTION_USE_ITEM:
    case TURN_ACTION_WAIT:
    case TURN_ACTION_INTERACT:
    default:
        break;
    }

    if (enemy_take_turn())
        return;

    s_turn_action = TURN_ACTION_NONE;
    s_room_turn_state = ROOM_TURN_WAIT_INPUT;
    room_draw_actors();
}

static void player_start_move(s8 dx, s8 dy, PlayerDirection dir)
{
    selector_hide();
    player_face(dir);
    s_player_target_gx = (u8)((s8)s_player_gx + dx);
    s_player_target_gy = (u8)((s8)s_player_gy + dy);
    s_player_step_x = (s8)(dx * PLAYER_MOVE_STEP);
    s_player_step_y = (s8)(dy * PLAYER_MOVE_STEP);
    s_player_anim_frame = 0u;
    s_player_anim_timer = 0u;
    s_turn_action = TURN_ACTION_MOVE;
    s_room_turn_state = ROOM_TURN_PLAYER_ANIM;
    room_draw_actors();
}

static void player_try_move(s8 dx, s8 dy, PlayerDirection dir)
{
    s8 nx;
    s8 ny;

    nx = (s8)((s8)s_player_gx + dx);
    ny = (s8)((s8)s_player_gy + dy);

    player_face(dir);

    if (s_static_room == STATIC_ROOM_01) {
        /* salle_01 -> dungeon (bank room 0, entree par le sud) via la porte nord.
         * salle_02 reste accessible en debug via le raccourci de s_static_room mais
         * n'est plus dans le flow de depart. */
        if (nx == (s8)ROOM_LINK_GX && ny == 0) {
            s_state = STATE_DUNGEON;
            return;
        }
    } else {
        /* salle_02 -> salle_01 via la porte sud. */
        if (nx == (s8)ROOM_LINK_GX && ny == (s8)ROOM_LINK_SOUTH_GY) {
            room_enter(STATIC_ROOM_01, ROOM_ENTRY_FROM_NORTH, 1u);
            return;
        }
        /* L'escalier de salle_02 menait au donjon procgen (retire). Pour
         * l'instant pas de transition : la prochaine etape est la banque de
         * salles statiques + cluster picker qui prendra le relais. */
    }

    if (room_action_target_at(nx, ny)) {
        selector_show((u8)nx, (u8)ny);
        room_draw_actors();
        return;
    }

    selector_hide();

    if (room_grid_cell_blocks(nx, ny)) {
        /* Bump: turn IS consumed even though the player doesn't move. */
        s_turn_action = TURN_ACTION_NONE;
        room_begin_enemy_turns();
        room_draw_actors();
        return;
    }

    player_start_move(dx, dy, dir);
}

static void player_start_wait(void)
{
    selector_hide();
    s_turn_action = TURN_ACTION_WAIT;
    s_player_anim_frame = PLAYER_IDLE_FRAME;
    s_player_anim_timer = 0u;
    room_begin_enemy_turns();
    room_draw_actors();
}

static void player_start_interact(void)
{
    selector_hide();
    s_turn_action = TURN_ACTION_INTERACT;
    room_begin_enemy_turns();
    room_draw_actors();
}

static void player_confirm_selector(void)
{
    u8 target_gx;
    u8 target_gy;

    if (!s_selector_visible)
        return;

    target_gx = s_selector_gx;
    target_gy = s_selector_gy;

    if (enemy_at_grid((s8)target_gx, (s8)target_gy)) {
        player_attack_enemy(target_gx, target_gy);
        return;
    }

    selector_hide();
    room_draw_actors();
}

static void player_update_move_anim(void)
{
    if (s_player_anim_timer < PLAYER_MOVE_ANIM_FRAMES) {
        s_player_x = (s16)(s_player_x + s_player_step_x);
        s_player_y = (s16)(s_player_y + s_player_step_y);
        s_player_anim_timer++;
        player_set_walk_frame();
    }

    if (s_player_anim_timer >= PLAYER_MOVE_ANIM_FRAMES) {
        s_player_gx = s_player_target_gx;
        s_player_gy = s_player_target_gy;
        s_player_x = player_grid_to_screen(s_player_gx);
        s_player_y = player_grid_to_screen(s_player_gy);
        s_player_step_x = 0;
        s_player_step_y = 0;
        s_player_anim_frame = PLAYER_IDLE_FRAME;
        room_begin_enemy_turns();
    }

    room_draw_actors();
}

static void room_finish_attack_resolution(AttackEffectSource source)
{
    if (source == ATTACK_EFFECT_PLAYER) {
        if (s_enemy_hp == 0u)
            s_enemy_active = 0u;

        if (s_enemy_active) {
            room_begin_enemy_turns();
        } else {
            s_turn_action = TURN_ACTION_NONE;
            s_room_turn_state = ROOM_TURN_WAIT_INPUT;
        }
    } else if (source == ATTACK_EFFECT_ENEMY) {
        s_turn_action = TURN_ACTION_NONE;
        s_room_turn_state = ROOM_TURN_WAIT_INPUT;
    } else {
        s_turn_action = TURN_ACTION_NONE;
        s_room_turn_state = ROOM_TURN_WAIT_INPUT;
    }

    room_draw_actors();
}

static void attack_effect_finish(void)
{
    AttackEffectSource source;

    source = s_attack_effect_source;
    attack_effect_hide();

    if (s_damage_popup_visible) {
        s_room_turn_state = ROOM_TURN_DAMAGE_POPUP;
        room_draw_actors();
        return;
    }

    room_finish_attack_resolution(source);
}

static void room_update_after_player_attack_delay(void)
{
    if (s_after_player_attack_delay > 0u) {
        s_after_player_attack_delay--;
        room_draw_actors();
        return;
    }

    room_begin_enemy_turns();
    room_draw_actors();
}

static void damage_popup_finish(void)
{
    AttackEffectSource source;

    source = s_damage_popup_source;
    damage_popup_hide();
    room_finish_attack_resolution(source);
}

static void room_update_damage_popup(void)
{
    if (!s_damage_popup_visible) {
        damage_popup_finish();
        return;
    }

    if (s_damage_popup_timer > 0u)
        s_damage_popup_timer--;

    if (s_damage_popup_timer == 0u) {
        damage_popup_finish();
        return;
    }

    room_draw_actors();
}

static void attack_effect_update(void)
{
    u8 duration;

    if (!s_attack_effect_visible) {
        attack_effect_finish();
        return;
    }

    s_attack_effect_timer++;

    if (s_damage_popup_visible && s_damage_popup_timer > 0u)
        s_damage_popup_timer--;

    duration = (u8)(ATTACK_EFFECT_FRAME_TICKS * 2u);

    if (s_attack_effect_timer >= duration) {
        attack_effect_finish();
        return;
    }

    room_draw_actors();
}

static void room_update_wait_input(void)
{
    u8 pad;

    if (g_player.hp == 0u)
        return;

    pad = (u8)(ngpc_pad_pressed | ngpc_pad_repeat);

    if (pad & PAD_UP) {
        player_try_move(0, -1, PLAYER_DIR_UP);
    } else if (pad & PAD_DOWN) {
        player_try_move(0, 1, PLAYER_DIR_DOWN);
    } else if (pad & PAD_LEFT) {
        player_try_move(-1, 0, PLAYER_DIR_LEFT);
    } else if (pad & PAD_RIGHT) {
        player_try_move(1, 0, PLAYER_DIR_RIGHT);
    } else if (ngpc_pad_pressed & PAD_B) {
        if (s_selector_visible) {
            selector_hide();
            room_draw_actors();
        } else {
            player_start_wait();
        }
    } else if (ngpc_pad_pressed & PAD_A) {
        player_confirm_selector();
    }
}

static void room_update_turn(void)
{
    switch (s_room_turn_state) {
    case ROOM_TURN_WAIT_INPUT:
        room_update_wait_input();
        break;
    case ROOM_TURN_PLAYER_ANIM:
        player_update_move_anim();
        break;
    case ROOM_TURN_ATTACK_EFFECT:
        attack_effect_update();
        break;
    case ROOM_TURN_DAMAGE_POPUP:
        room_update_damage_popup();
        break;
    case ROOM_TURN_AFTER_PLAYER_ATTACK_DELAY:
        room_update_after_player_attack_delay();
        break;
    case ROOM_TURN_ENEMY_TURNS:
        room_run_enemy_turns();
        break;
    case ROOM_TURN_ENEMY_ANIM:
        enemy_update_move_anim();
        break;
    }
}

static void intro_init(void)
{
    /* Pas de HUD : desactive le scroll table SCR2 du raster ISR. L'ISR
     * Timer0 continue de fire mais ne touche plus aux scroll registers,
     * donc HW_SCR2_OFS_X/Y restent ce que ngpc_gfx_scroll() pose. */
    hud_raster_disable();
    ngpc_gfx_scroll(GFX_SCR1, 0, 0);
    ngpc_gfx_scroll(GFX_SCR2, 0, 0);
    ngpc_gfx_clear(GFX_SCR1);
    ngpc_gfx_clear(GFX_SCR2);
    ngpc_sprite_hide_all();
    ngpc_gfx_set_bg_color(RGB(0, 0, 0));
    NGP_TILEMAP_BLIT_SCR1(intro_ngpc_craft_png, INTRO_TILE_BASE);
}

static void intro_update(void)
{
    if (ngpc_pad_pressed & PAD_A) {
        s_state = STATE_MENU;
    }
}

/* ---- State: Menu + BGM ---- */

#define MENU_RESTORE_ROW(x, y, w, tile_base)                                      \
    do {                                                                         \
        u8 __i;                                                                  \
        for (__i = 0; __i < (u8)(w); __i++) {                                    \
            u16 __src = (u16)((u16)(y) * menu_kuroi_dokutsu_scr1_map_w +         \
                (u16)(x) + (u16)__i);                                            \
            u16 __dst = (u16)((u16)(y) * SCR_MAP_W + (u16)(x) + (u16)__i);       \
            u16 __tile = (u16)((u16)(tile_base) +                                \
                menu_kuroi_dokutsu_scr1_map_tiles[__src]);                      \
            u8 __pal = (u8)(menu_kuroi_dokutsu_scr1_map_pals[__src] & 0x0Fu);   \
            HW_SCR1_MAP[__dst] = SCR_TILE(__tile, __pal);                       \
        }                                                                        \
    } while (0)

#define MENU_DRAW_SELECT(prefix, x, y, tile_base)                                \
    do {                                                                         \
        u8 __i;                                                                  \
        for (__i = 0; __i < (u8)(prefix##_map_len); __i++) {                    \
            u16 __dst = (u16)((u16)(y) * SCR_MAP_W + (u16)(x) + (u16)__i);       \
            u16 __tile = (u16)((u16)(tile_base) + prefix##_map_tiles[__i]);      \
            HW_SCR1_MAP[__dst] = SCR_TILE(__tile, MENU_SELECT_PAL);             \
        }                                                                        \
    } while (0)

static void menu_restore_selection(MenuSelection sel, u16 tile_base)
{
    switch (sel) {
    case MENU_SEL_START:
        MENU_RESTORE_ROW(7u, 9u, menu_select_start_map_w, tile_base);
        break;
    case MENU_SEL_CONTINUE:
        MENU_RESTORE_ROW(7u, 11u, menu_select_continue_map_w, tile_base);
        break;
    case MENU_SEL_OPTIONS:
        MENU_RESTORE_ROW(7u, 13u, menu_select_options_map_w, tile_base);
        break;
    default:
        break;
    }
}

static void menu_draw_selection(MenuSelection sel)
{
    switch (sel) {
    case MENU_SEL_START:
        MENU_DRAW_SELECT(menu_select_start, 7u, 9u, menu_select_start_tile_base());
        break;
    case MENU_SEL_CONTINUE:
        MENU_DRAW_SELECT(menu_select_continue, 7u, 11u, menu_select_continue_tile_base());
        break;
    case MENU_SEL_OPTIONS:
        MENU_DRAW_SELECT(menu_select_options, 7u, 13u, menu_select_options_tile_base());
        break;
    default:
        break;
    }
}

static void menu_load_selection_tiles(void)
{
    NGP_TILEMAP_LOAD_TILES_VRAM(menu_select_start, menu_select_start_tile_base());
    NGP_TILEMAP_LOAD_TILES_VRAM(menu_select_continue, menu_select_continue_tile_base());
    NGP_TILEMAP_LOAD_TILES_VRAM(menu_select_options, menu_select_options_tile_base());

    ngpc_gfx_set_palette(GFX_SCR1, MENU_SELECT_PAL,
        menu_select_start_palettes[0],
        menu_select_start_palettes[1],
        menu_select_start_palettes[2],
        menu_select_start_palettes[3]);
}

static void menu_apply_selection(void)
{
    if (s_menu_last_selection != MENU_SEL_COUNT) {
        menu_restore_selection(s_menu_last_selection, menu_tile_base());
    }

    menu_draw_selection(s_menu_selection);
    s_menu_last_selection = s_menu_selection;
}

static void menu_accept_selection(void)
{
    switch (s_menu_selection) {
    case MENU_SEL_START:
        s_state = STATE_LEVEL_SELECT;
        break;
    case MENU_SEL_CONTINUE:
        /* SAVE-2 : la save flash a deja ete chargee au boot (cf main()
         * mkd_save_init apres player_state_reset). L'etat RAM
         * (g_player.atk_base/hp_max/crit_chance + s_level_select_unlocked)
         * reflete deja la save. CONTINUE = simple transition vers LEVEL_SELECT.
         * Si pas de save valide -> etat par defaut (bases + slot 0 seul) =>
         * level select ne montre que le slot 0 accessible. */
        s_state = STATE_LEVEL_SELECT;
        break;
    case MENU_SEL_OPTIONS:
        s_state = STATE_OPTIONS;  /* MKD-8 v2 */
        break;
    default:
        break;
    }
}

static void menu_init(void)
{
    u16 tile_base;

    /* Pas de HUD : desactive le scroll table SCR2 du raster ISR. */
    hud_raster_disable();
    ngpc_gfx_set_viewport(0, 0, SCREEN_W, SCREEN_H);
    HW_SCR_PRIO = 0x00; /* SCR1 front, SCR2 behind */

    ngpc_gfx_scroll(GFX_SCR1, 0, 0);
    ngpc_gfx_scroll(GFX_SCR2, 0, 0);
    ngpc_gfx_clear(GFX_SCR1);
    ngpc_gfx_clear(GFX_SCR2);
    ngpc_sprite_hide_all();
    ngpc_gfx_set_bg_color(RGB(0, 0, 0));

    tile_base = menu_tile_base();
    NGP_TILEMAP_LOAD_TILES_VRAM(menu_kuroi_dokutsu, tile_base);
    NGP_TILEMAP_LOAD_PALETTES_SCR1(menu_kuroi_dokutsu_scr1);
    NGP_TILEMAP_LOAD_PALETTES_SCR2(menu_kuroi_dokutsu_scr2);
    NGP_TILEMAP_PUT_MAP_SCR2(menu_kuroi_dokutsu_scr2, tile_base);
    NGP_TILEMAP_PUT_MAP_SCR1(menu_kuroi_dokutsu_scr1, tile_base);

    menu_load_selection_tiles();
    s_menu_selection = MENU_SEL_START;
    s_menu_last_selection = MENU_SEL_COUNT;
    menu_apply_selection();

    Bgm_SetNoteTable(NOTE_TABLE);
    Bgm_StartLoop4Ex(
        BGM_CH0, BGM_CH0_LOOP,
        BGM_CH1, BGM_CH1_LOOP,
        BGM_CH2, BGM_CH2_LOOP,
        BGM_CHN, BGM_CHN_LOOP
    );
}

static void menu_update(void)
{
    if (ngpc_pad_pressed & PAD_A) {
        menu_accept_selection();
    }

    if (ngpc_pad_pressed & PAD_UP) {
        if (s_menu_selection > MENU_SEL_START) {
            s_menu_selection = (MenuSelection)((u8)s_menu_selection - 1u);
            menu_apply_selection();
        }
    } else if (ngpc_pad_pressed & PAD_DOWN) {
        if (s_menu_selection < MENU_SEL_OPTIONS) {
            s_menu_selection = (MenuSelection)((u8)s_menu_selection + 1u);
            menu_apply_selection();
        }
    }
}

/* ---- State: Level Select ---- */

static void level_select_init(void)
{
    /* Pas de HUD : desactive le scroll table SCR2 du raster ISR. */
    hud_raster_disable();
    ngpc_gfx_set_viewport(0, 0, SCREEN_W, SCREEN_H);
    HW_SCR_PRIO = 0x00; /* SCR1 front, SCR2 behind */

    ngpc_gfx_scroll(GFX_SCR1, 0, 0);
    ngpc_gfx_scroll(GFX_SCR2, 0, 0);
    ngpc_gfx_clear(GFX_SCR1);
    ngpc_gfx_clear(GFX_SCR2);
    ngpc_sprite_hide_all();
    ngpc_gfx_set_bg_color(RGB(0, 0, 0));

    NGP_TILEMAP_LOAD_TILES_VRAM(menu_level_select, LEVEL_SELECT_TILE_BASE);
    NGP_TILEMAP_LOAD_TILES_VRAM(menu_level_pointer, menu_level_pointer_tile_base);
    NGP_TILEMAP_LOAD_TILES_VRAM(menu_level_lock, menu_level_lock_tile_base);
    NGP_TILEMAP_LOAD_PALETTES_SCR1(menu_level_select_scr1);
    NGP_TILEMAP_LOAD_PALETTES_SCR2(menu_level_select_scr2);
    level_select_pointer_load_palettes();
    level_select_lock_load_palettes();
    NGP_TILEMAP_PUT_MAP_SCR2(menu_level_select_scr2, LEVEL_SELECT_TILE_BASE);
    NGP_TILEMAP_PUT_MAP_SCR1(menu_level_select_scr1, LEVEL_SELECT_TILE_BASE);
    s_level_select_index = 0u;
    level_select_pointer_update_position();
    level_select_draw_ui();
}

static void level_select_update(void)
{
    u8 pad;

    pad = (u8)(ngpc_pad_pressed | ngpc_pad_repeat);

    if (ngpc_pad_pressed & PAD_B) {
        s_state = STATE_MENU;
        return;
    }

    if (ngpc_pad_pressed & PAD_A) {
        if (!level_select_is_unlocked(s_level_select_index))
            return;

        if (s_level_select_index == 0u) {
            ngpc_rng_seed();
            ngpc_qrandom_init();
            s_static_room = STATIC_ROOM_01;
            s_static_room_entry = ROOM_ENTRY_DEFAULT;
            s_state = STATE_ROOM;
        }
        return;
    }

    if ((pad & PAD_LEFT) && s_level_select_index > 0u) {
        s_level_select_index = (u8)(s_level_select_index - 1u);
        level_select_pointer_update_position();
        level_select_draw_ui();
    } else if ((pad & PAD_RIGHT) &&
        s_level_select_index < (u8)(LEVEL_SELECT_ENTRY_COUNT - 1u)) {
        s_level_select_index = (u8)(s_level_select_index + 1u);
        level_select_pointer_update_position();
        level_select_draw_ui();
    }
}

/* ---- State: Room ---- */

/* Reinstalle la palette font ahchay (SCR2 pal 0) via le pattern array+loop
 * qui contourne le bug cc900 "4 RGB() immediates". Le macro
 * ahchay_font_set_palette() est casse, et entre boot et STATE_ROOM les
 * menus (kuroi_dokutsu/level_select) ecrasent SCR2 pal 0 via
 * NGP_TILEMAP_LOAD_PALETTES_SCR2 -- donc il faut re-installer ici.
 * 2 slots emis (1 ne suffit pas : cc900 constant-folde et retombe dans le
 * bug). Slot 1 SCR2 n'est utilise par aucun texte du projet. */
static void install_font_palette_scr2(void)
{
    /* 2 slots emis (slot 0 = font transparente). Slot 1 dummy pour eviter
     * le bug cc900 constant-fold. Slot 2 = HUD_BG_PAL_SCR2 NE PAS toucher
     * (geree par hud_load_vram). Slot 3 = font opaque HUD geree par
     * hud_font_install_palettes(). */
    static const u16 NGP_FAR pal[8] = {
        0x0000, 0x0FFF, 0x0000, 0x0000,  /* slot 0 font transparente */
        0x0000, 0x0FFF, 0x0000, 0x0000,  /* slot 1 dummy */
    };
    u8 i;
    u16 src;
    for (i = 0u; i < 2u; i++) {
        src = (u16)i * 4u;
        ngpc_gfx_set_palette(GFX_SCR2, i,
            pal[src],
            pal[(u16)(src + 1u)],
            pal[(u16)(src + 2u)],
            pal[(u16)(src + 3u)]);
    }
}

static void room_load_vram(void)
{
    /* Viewport plein ecran : la window NGPC clip aussi les sprites,
     * donc on NE PEUT PAS la reduire pour faire de la place au HUD.
     * Le HUD sprite vient se peindre par-dessus la BG salle_01 dans
     * la zone basse (les rangees 17-18 de salle_01 restent dessous,
     * cachees par le HUD - a accepter pour l'instant ou redessiner
     * salle_01 en 17 rows). */
    ngpc_gfx_set_viewport(0, 0, SCREEN_W, SCREEN_H);
    HW_SCR_PRIO = 0x80; /* SCR2 front for damage popups, SCR1 room behind */

    ngpc_gfx_scroll(GFX_SCR1, 0, 0);
    ngpc_gfx_scroll(GFX_SCR2, 0, 0);
    ngpc_gfx_clear(GFX_SCR1);
    ngpc_gfx_clear(GFX_SCR2);
    ngpc_sprite_hide_all();
    ngpc_gfx_set_bg_color(RGB(0, 0, 0));

    switch (s_static_room) {
    case STATIC_ROOM_02:
        NGP_TILEMAP_BLIT_SCR1(salle_02, ROOM_TILE_BASE);
        break;
    case STATIC_ROOM_01:
    default:
        NGP_TILEMAP_BLIT_SCR1(salle_01, ROOM_TILE_BASE);
        break;
    }
    NGP_TILEMAP_LOAD_TILES_VRAM(player_topdown, player_topdown_tile_base);
    NGP_TILEMAP_LOAD_TILES_VRAM(lime_sheet, lime_sheet_tile_base);
    NGP_TILEMAP_LOAD_TILES_VRAM(selecteur, selecteur_tile_base);
    NGP_TILEMAP_LOAD_TILES_VRAM(effect_attaque, effect_attaque_tile_base);
    NGP_TILEMAP_LOAD_TILES_VRAM(effect_attaque_crit, effect_attaque_crit_tile_base);
    /* skull_death : asset dedie au popup de mort fatale (different du
     * sprite "skull" qui est l'enemy SKULL du dungeon). 4 tiles 16x16. */
    NGP_TILEMAP_LOAD_TILES_VRAM(skull_death, skull_death_tile_base);
    player_load_palettes();
    enemy_load_palettes();
    selector_load_palettes();
    attack_effect_load_palettes();
    attack_effect_crit_load_palettes();
    skull_death_load_palettes();
    /* SCR2 pal 0 a ete ecrasee par les menus (NGP_TILEMAP_LOAD_PALETTES_SCR2
     * dans menu_init/level_select_init) -> reinstaller la palette font
     * via le pattern array+loop. NE PAS utiliser ahchay_font_set_palette
     * (4 RGB() immediates -> casse par cc900). */
    install_font_palette_scr2();
    /* La font opaque (slot 416..511) a aussi ete ecrasee par menu_kuroi_dokutsu
     * (176..500) -> reinstaller avant rendu HUD. */
    hud_font_install_opaque_variant();
    hud_font_install_palettes(GFX_SCR2);
    hud_load_vram();
    /* STATE_ROOM ne scrolle pas. Scroll SCR2 deja a (0,0) via ngpc_gfx_scroll
     * appele en amont (cf room_load_vram). hud_raster_update arme le split
     * HUD a ligne 136. */
    hud_raster_update();
    hud_paint_bg();
}

static void room_enter(StaticRoomId room_id, StaticRoomEntry entry, u8 keep_hp)
{
    s_static_room = room_id;
    s_static_room_entry = entry;
    room_load_vram();
    player_place_for_room_entry(keep_hp);
    enemy_place_for_current_room();
    selector_hide();
    damage_popup_hide();
    attack_effect_hide();
    room_draw_actors();
}

static void room_init(void)
{
    /* MKD-depth : salle_01 = "hub" hors donjon. Reset depth_current pour
     * que le HUD n'affiche pas la profondeur stale du run precedent.
     * dungeon_init reset aussi mais ne couvre que la transition direct
     * depuis menu -> dungeon. Ici on couvre la transition normale
     * menu -> salle_01 -> dungeon. Stats du run reset aussi pour
     * synchroniser avec un nouveau debut de partie. */
    g_depth_current = 0u;
    g_run_kills = 0u;
    g_run_turns = 0u;

    room_enter(s_static_room, s_static_room_entry, 0u);
}

static void room_resume(void)
{
    /* Returning from STATE_PAUSE: VRAM was overwritten by the pause menu,
     * so reload all graphics assets — but keep the player/enemy state. */
    room_load_vram();
    room_draw_actors();
}

static void room_update(void)
{
    /* Sequence de mort en cours : on freeze la state machine room mais
     * on continue de dessiner la frame (room_update_turn appelle aussi
     * draw_actors -> on le skip ici). update_death_sequence() s'occupe
     * de la transition. */
    if (s_death_phase != 0u) {
        room_draw_actors();
        return;
    }

    if (ngpc_pad_pressed & PAD_OPTION) {
        s_pause_return_state = STATE_ROOM;
        s_state = STATE_PAUSE;
        return;
    }

    room_update_turn();
}

/* ---- State: Pause ---- */

static PauseSelection s_pause_selection = PAUSE_SEL_ITEM;
static u8 s_pause_cursor_frame = 0u;
static u8 s_pause_cursor_timer = 0u;

static u8 pause_cursor_y(PauseSelection sel)
{
    /* MKD-5 : 4 entrees ITEM/MAP/SAVE/QUIT — spacing 32px.
     * Bandes de texte centrees aux y={28,61,92,123}. Cursor sprite 16x16,
     * y = text_center - 8 = centre sprite sur centre texte. */
    switch (sel) {
    case PAUSE_SEL_ITEM: return 24u;
    case PAUSE_SEL_MAP:  return 56u;
    case PAUSE_SEL_SAVE: return 88u;
    case PAUSE_SEL_QUIT: return 120u;
    default:             return 24u;
    }
}

static void pause_cursor_load_palettes(void)
{
    u8 i;
    u16 src;

    for (i = 0; i < select_menu_pause_palette_count; i++) {
        src = (u16)i * 4u;
        ngpc_gfx_set_palette(GFX_SPR, (u8)(select_menu_pause_pal_base + i),
            select_menu_pause_palettes[src],
            select_menu_pause_palettes[(u16)(src + 1u)],
            select_menu_pause_palettes[(u16)(src + 2u)],
            select_menu_pause_palettes[(u16)(src + 3u)]);
    }
}

static void pause_draw_cursor(void)
{
    const NgpcMetasprite *frame;

    frame = (s_pause_cursor_frame == 0u) ?
        &select_menu_pause_frame_0 : &select_menu_pause_frame_1;

    ngpc_mspr_draw((u8)PAUSE_CURSOR_SPR_BASE,
        (s16)40, (s16)pause_cursor_y(s_pause_selection),
        frame, (u8)SPR_FRONT);
}

static void pause_init(void)
{
    /* Pas de HUD : desactive le scroll table SCR2 du raster ISR. */
    hud_raster_disable();
    ngpc_gfx_set_viewport(0, 0, SCREEN_W, SCREEN_H);
    HW_SCR_PRIO = 0x00; /* SCR1 front, SCR2 behind — consistent with the other menus */
    ngpc_gfx_scroll(GFX_SCR1, 0, 0);
    ngpc_gfx_scroll(GFX_SCR2, 0, 0);
    ngpc_gfx_clear(GFX_SCR1);
    ngpc_gfx_clear(GFX_SCR2);
    ngpc_sprite_hide_all();
    ngpc_gfx_set_bg_color(RGB(0, 0, 0));

    NGP_TILEMAP_LOAD_TILES_VRAM(menu_pause, PAUSE_TILE_BASE);
    NGP_TILEMAP_LOAD_PALETTES_SCR1(menu_pause_scr1);
    NGP_TILEMAP_LOAD_PALETTES_SCR2(menu_pause_scr2);
    NGP_TILEMAP_PUT_MAP_SCR2(menu_pause_scr2, PAUSE_TILE_BASE);
    NGP_TILEMAP_PUT_MAP_SCR1(menu_pause_scr1, PAUSE_TILE_BASE);

    /* Load the cursor tiles + palette. */
    ngpc_gfx_load_tiles_at(select_menu_pause_tiles,
        select_menu_pause_tiles_count,
        select_menu_pause_tile_base);
    pause_cursor_load_palettes();

    s_pause_selection = PAUSE_SEL_ITEM;
    s_pause_cursor_frame = 0u;
    s_pause_cursor_timer = 0u;
    pause_draw_cursor();

    /* MKD-misc : reinstaller font opaque (slot 416 peut avoir ete ecrasee). */
    hud_font_install_opaque_variant();
    hud_font_install_palettes(GFX_SCR2);

    /* MKD-8 v2 : seed affichee en bas-gauche, continu durant pause.
     * Row 17 col 0-8 sur SCR2. Variante font opaque (bg noir). */
    hud_text_print(GFX_SCR2, 0u, 17u, "SEED:");
    if (g_last_cluster_seed == 0u && g_dungeon_seed == 0u) {
        hud_text_print(GFX_SCR2, 5u, 17u, "----");
    } else {
        hud_text_print_num(GFX_SCR2, 5u, 17u, g_last_cluster_seed, 3u);
    }
    /* MKD-depth : stats persistantes (visibles pour verifier que les
     * upgrades sont bien appliques). Row 17 col 10-18. */
    hud_text_print    (GFX_SCR2, 10u, 17u, "ATK:");
    hud_text_print_num(GFX_SCR2, 14u, 17u, (u16)g_player.atk_base, 2u);
}

static void pause_update(void)
{
    /* Close pause: OPTION or B returns to wherever pause was opened from
     * (STATE_ROOM par defaut, STATE_DUNGEON si ouvert depuis le donjon). */
    if (ngpc_pad_pressed & (PAD_OPTION | PAD_B)) {
        s_state = s_pause_return_state;
        return;
    }

    if (ngpc_pad_pressed & PAD_UP) {
        if (s_pause_selection > PAUSE_SEL_ITEM) {
            s_pause_selection = (PauseSelection)((u8)s_pause_selection - 1u);
        }
    } else if (ngpc_pad_pressed & PAD_DOWN) {
        if (s_pause_selection < PAUSE_SEL_QUIT) {
            s_pause_selection = (PauseSelection)((u8)s_pause_selection + 1u);
        }
    }

    if (ngpc_pad_pressed & PAD_A) {
        switch (s_pause_selection) {
        case PAUSE_SEL_ITEM:
            /* MKD-inv : ouvre l'ecran inventaire, retour au pause apres B. */
            s_state = STATE_INVENTORY;
            return;
        case PAUSE_SEL_MAP:
            /* MKD-5 : ouvre la minimap cluster, retour au pause apres B. */
            s_minimap_return_state = STATE_PAUSE;
            s_state = STATE_MINIMAP;
            return;
        case PAUSE_SEL_SAVE:
            /* TODO: flash save — non implémenté (build: NGP_ENABLE_FLASH_SAVE=0). */
            break;
        case PAUSE_SEL_QUIT:
            s_state = STATE_MENU;
            return;
        default:
            break;
        }
    }

    /* Animate the 2-frame cursor. */
    s_pause_cursor_timer++;
    if (s_pause_cursor_timer >= PAUSE_CURSOR_ANIM_TICKS) {
        s_pause_cursor_timer = 0u;
        s_pause_cursor_frame ^= 1u;
    }
    pause_draw_cursor();
}


/* =========================================================================
 * MKD-inv : State Inventaire - ecran accessible via ITEM dans la pause
 * =========================================================================
 * Affiche GraphX/inventaire.png plein ecran sur SCR1 (6 slots items en haut
 * + actions use/equip/discard en bas-gauche + stats hp/def/att/cha).
 *
 * Deux phases :
 *   PHASE_ACTION : un curseur fleche (sprite select_menu_pause, le meme que
 *                  le menu pause) navigue UP/DOWN entre use/equip/discard.
 *                  A -> passe en PHASE_ITEM. B/OPTION -> ferme (retour pause).
 *   PHASE_ITEM   : le cadre inventaire_selector_item (32x32) prend le relais
 *                  et s'affiche en overlay sur l'un des 6 slots (grille 3x2).
 *                  D-pad navigue, A valide l'item (applique l'action choisie),
 *                  B annule (retour PHASE_ACTION), OPTION ferme tout.
 *
 * Overlay non-gameplay : reset complet du contexte video (comme
 * game_over_init), VRAM rechargee au retour vers le donjon/salle.
 * ========================================================================= */

typedef enum {
    INV_PHASE_ACTION = 0,
    INV_PHASE_ITEM
} InvPhase;

typedef enum {
    INV_ACT_USE = 0,
    INV_ACT_EQUIP,
    INV_ACT_DISCARD,
    INV_ACT_COUNT
} InvAction;

#define INV_NAV_SPR_BASE      0u    /* curseur fleche (2 sub-sprites : 0..1) */
#define INV_ITEM_SPR_BASE     4u    /* cadre selecteur item (12 sub-sprites -> 4..15) */
#define INV_ITEM_ICON_SPR_BASE 16u  /* 6 icones 16x16 -> 4 OAM chacune -> 16..39 */
#define INV_CURSOR_ANIM_TICKS 15u
#define INV_ITEM_COLS         3u
#define INV_ITEM_ROWS         2u

static InvPhase s_inv_phase = INV_PHASE_ACTION;
static u8 s_inv_action = 0u;        /* 0..2 (use/equip/discard) */
static u8 s_inv_item = 0u;          /* 0..5 (row*3 + col) */
static u8 s_inv_cursor_frame = 0u;
static u8 s_inv_cursor_timer = 0u;

/* y du coin haut-gauche du curseur fleche (8px haut) pour chaque action.
 * Centres des labels mesures sur inventaire.png (police compacte 2026-05-31) :
 * Use~107, Equip~123, Discard~139 -> y = centre - 4. */
static s16 inv_action_cursor_y(u8 act)
{
    switch (act) {
    case INV_ACT_USE:     return (s16)103;
    case INV_ACT_EQUIP:   return (s16)119;
    case INV_ACT_DISCARD: return (s16)135;
    default:              return (s16)103;
    }
}

/* coin haut-gauche du slot item idx (grille 3 cols x 2 rows, slots 32x32,
 * positions mesurees sur inventaire.png). */
static void inv_item_slot_xy(u8 idx, s16 *out_x, s16 *out_y)
{
    static const s16 col_x[INV_ITEM_COLS] = { 16, 64, 112 };
    static const s16 row_y[INV_ITEM_ROWS] = { 8, 48 };
    *out_x = col_x[idx % INV_ITEM_COLS];
    *out_y = row_y[idx / INV_ITEM_COLS];
}

static void inv_load_cursor_assets(void)
{
    u8 i;
    u16 src;

    /* Curseur de navigation = sprite select_menu_pause (reutilise du pause). */
    ngpc_gfx_load_tiles_at(select_menu_pause_tiles,
        select_menu_pause_tiles_count, select_menu_pause_tile_base);
    for (i = 0u; i < select_menu_pause_palette_count; i++) {
        src = (u16)i * 4u;
        ngpc_gfx_set_palette(GFX_SPR, (u8)(select_menu_pause_pal_base + i),
            select_menu_pause_palettes[src],
            select_menu_pause_palettes[(u16)(src + 1u)],
            select_menu_pause_palettes[(u16)(src + 2u)],
            select_menu_pause_palettes[(u16)(src + 3u)]);
    }

    /* Cadre selecteur d'item (overlay sur slot). */
    ngpc_gfx_load_tiles_at(inventaire_selector_item_tiles,
        inventaire_selector_item_tiles_count, inventaire_selector_item_tile_base);
    for (i = 0u; i < inventaire_selector_item_palette_count; i++) {
        src = (u16)i * 4u;
        ngpc_gfx_set_palette(GFX_SPR, (u8)(inventaire_selector_item_pal_base + i),
            inventaire_selector_item_palettes[src],
            inventaire_selector_item_palettes[(u16)(src + 1u)],
            inventaire_selector_item_palettes[(u16)(src + 2u)],
            inventaire_selector_item_palettes[(u16)(src + 3u)]);
    }
}

/* Charge tiles + palettes des icones d'items (potion, antidote) sur SPR. */
static void inv_load_item_assets(void)
{
    u8 i;
    u16 src;

    ngpc_gfx_load_tiles_at(item_potion_tiles,
        item_potion_tiles_count, item_potion_tile_base);
    for (i = 0u; i < item_potion_palette_count; i++) {
        src = (u16)i * 4u;
        ngpc_gfx_set_palette(GFX_SPR, (u8)(item_potion_pal_base + i),
            item_potion_palettes[src],
            item_potion_palettes[(u16)(src + 1u)],
            item_potion_palettes[(u16)(src + 2u)],
            item_potion_palettes[(u16)(src + 3u)]);
    }

    ngpc_gfx_load_tiles_at(item_antidote_tiles,
        item_antidote_tiles_count, item_antidote_tile_base);
    for (i = 0u; i < item_antidote_palette_count; i++) {
        src = (u16)i * 4u;
        ngpc_gfx_set_palette(GFX_SPR, (u8)(item_antidote_pal_base + i),
            item_antidote_palettes[src],
            item_antidote_palettes[(u16)(src + 1u)],
            item_antidote_palettes[(u16)(src + 2u)],
            item_antidote_palettes[(u16)(src + 3u)]);
    }
}

/* Metasprite icone pour un item_id (0 si pas d'icone connue). */
static const NgpcMetasprite *inv_item_icon_frame(u8 item_id)
{
    switch (item_id) {
    case ITEM_ID_POTION:   return &item_potion_frame_0;
    case ITEM_ID_ANTIDOTE: return &item_antidote_frame_0;
    default:               return (const NgpcMetasprite *)0;
    }
}

/* Nombre de sub-sprites de chaque curseur (pour les hides cibles). */
#define INV_NAV_SPR_COUNT   2u    /* fleche 16x8 -> 2 tiles */
#define INV_ITEM_SPR_COUNT  12u   /* cadre 32x32 creux -> 12 sub-sprites */

/* Dessine les 6 icones items (STATIQUES). hide_all + redraw : appele
 * seulement a l'init (rare). Les 6 slots visibles mappent g_player.inv[0..5].
 * NE PAS appeler chaque frame -> evite le flicker (trop d'ecritures OAM avant
 * que le faisceau n'atteigne la rangee du haut). */
static void inv_draw_items_all(void)
{
    u8 i, item_id;
    s16 sx, sy;
    const NgpcMetasprite *frame;

    ngpc_sprite_hide_all();

    for (i = 0u; i < 6u; i++) {
        item_id = g_player.inv[i].item_id;
        if (item_id == 0u) continue;
        frame = inv_item_icon_frame(item_id);
        if (frame == (const NgpcMetasprite *)0) continue;
        inv_item_slot_xy(i, &sx, &sy);
        /* icone 16x16 centree dans le slot 32x32 (offset +8,+8). */
        ngpc_mspr_draw((u8)(INV_ITEM_ICON_SPR_BASE + (u8)(i * 4u)),
            (s16)(sx + 8), (s16)(sy + 8), frame, (u8)SPR_FRONT);
    }
}

/* Met a jour UN slot d'icone apres une action (use/discard/equip) : efface
 * ses 4 OAM puis redessine si le slot contient encore un item. */
static void inv_refresh_item_slot(u8 slot)
{
    u8 base;
    s16 sx, sy;
    const NgpcMetasprite *frame;

    if (slot >= 6u) return;
    base = (u8)(INV_ITEM_ICON_SPR_BASE + (u8)(slot * 4u));
    ngpc_mspr_hide(base, 4u);
    if (g_player.inv[slot].item_id != 0u) {
        frame = inv_item_icon_frame(g_player.inv[slot].item_id);
        if (frame != (const NgpcMetasprite *)0) {
            inv_item_slot_xy(slot, &sx, &sy);
            ngpc_mspr_draw(base, (s16)(sx + 8), (s16)(sy + 8), frame, (u8)SPR_FRONT);
        }
    }
}

/* Dessine SEULEMENT le curseur actif, chaque frame, sur ses slots OAM fixes
 * (nav 0..1 / cadre 4..15). Les icones (16..39) ne sont pas touchees -> churn
 * OAM minimal (<=12 ecritures) -> pas de flicker. Les deux curseurs etant sur
 * des slots disjoints, l'inactif est masque explicitement au changement de
 * phase (cf inventory_update). */
static void inv_draw_cursor(void)
{
    const NgpcMetasprite *frame;
    s16 sx, sy;

    if (s_inv_phase == INV_PHASE_ACTION) {
        frame = (s_inv_cursor_frame == 0u) ?
            &select_menu_pause_frame_0 : &select_menu_pause_frame_1;
        /* fleche juste a gauche des labels (decalee a x=8 pour serrer le texte). */
        ngpc_mspr_draw((u8)INV_NAV_SPR_BASE, (s16)8,
            inv_action_cursor_y(s_inv_action), frame, (u8)SPR_FRONT);
    } else {
        inv_item_slot_xy(s_inv_item, &sx, &sy);
        ngpc_mspr_draw((u8)INV_ITEM_SPR_BASE, sx, sy,
            &inventaire_selector_item_frame_0, (u8)SPR_FRONT);
    }
}

/* Affiche les 4 stats joueur en face des labels deja dessines dans le fond.
 * Positions (col,row tiles) mesurees sur inventaire.png (police compacte
 * 2026-05-31) :
 *   ligne haute (row 13, centre y107) : "hp" -> col 11    "def" -> col 17
 *   ligne basse (row 16, centre y131) : "Att" -> col 11   "cha" -> col 17
 * (les 2 lignes stats sont plus espacees que les labels gauche : 107 puis 131.)
 * Champ 2 digits (espaces pour les zeros de tete, aligne a droite). */
static void inv_draw_stats(void)
{
    u8 cha;

    ngpc_text_print_num(GFX_SCR2, 0u, 11u, 13u, (u16)g_player.hp, 2u);
    ngpc_text_print_num(GFX_SCR2, 0u, 17u, 13u, (u16)player_state_def_total(), 2u);

    ngpc_text_print_num(GFX_SCR2, 0u, 11u, 16u, (u16)player_state_atk_total(), 2u);

    cha = player_state_crit_chance();
    if (cha > 99u) cha = 99u;  /* place serree : 2 digits max (cha 100% -> 99) */
    ngpc_text_print_num(GFX_SCR2, 0u, 17u, 16u, (u16)cha, 2u);
}

/* Applique l'action choisie a l'item du slot (item = index 0..5 = g_player.inv).
 * use=consomme l'effet (potion heal 5-8 / antidote cure poison), equip=equipe
 * (no-op pour les consumables), discard=jette le stack. */
static void inventory_apply_action(u8 action, u8 item)
{
    switch (action) {
    case INV_ACT_USE:
        (void)player_state_use_item(item);
        break;
    case INV_ACT_EQUIP:
        player_state_equip(item);  /* consumables : sans effet */
        break;
    case INV_ACT_DISCARD:
        player_state_inv_remove(item);
        break;
    default:
        break;
    }
    /* Stats SCR2 peuvent avoir change (HP apres une potion) -> refresh. */
    inv_draw_stats();
    /* L'icone du slot a pu disparaitre (use/discard) ou changer (equip swap). */
    inv_refresh_item_slot(item);
}

static void inventory_init(void)
{
    /* Reset video context comme pause_init / game_over_init. */
    hud_raster_disable();
    ngpc_gfx_set_viewport(0, 0, SCREEN_W, SCREEN_H);
    /* SCR2 DEVANT : le fond inventaire (SCR1) est opaque, donc on affiche les
     * chiffres de stats sur SCR2 par-dessus (pattern "room", cf room_load_vram). */
    HW_SCR_PRIO = 0x80;
    ngpc_gfx_scroll(GFX_SCR1, 0, 0);
    ngpc_gfx_scroll(GFX_SCR2, 0, 0);
    ngpc_gfx_clear(GFX_SCR1);
    ngpc_gfx_clear(GFX_SCR2);
    ngpc_sprite_hide_all();
    ngpc_gfx_set_bg_color(RGB(0, 0, 0));

    /* Fond inventaire sur SCR1. NGP_TILEMAP_BLIT_SCR1 charge tiles +
     * palettes (array+loop, safe vis-a-vis du bug cc900 "4 RGB() immediates")
     * + map en un seul appel. */
    NGP_TILEMAP_BLIT_SCR1(inventaire, INVENTORY_TILE_BASE);

    inv_load_cursor_assets();
    inv_load_item_assets();

    /* Police (tiles 32..127) + palette police standard sur SCR2 slot 0
     * (idx1 contour blanc, idx2 noir) : IDENTIQUE aux damage popups
     * (cf damage_popup_draw -> ngpc_text_print_num GFX_SCR2 pal 0). Donne un
     * chiffre noir a contour blanc, lisible sur le panneau. NE PAS mettre
     * idx1+idx2 a noir -> contour+remplissage fusionnent en bloc noir plein. */
    ahchay_font_load();
    install_font_palette_scr2();
    inv_draw_stats();

    s_inv_phase = INV_PHASE_ACTION;
    s_inv_action = 0u;
    s_inv_item = 0u;
    s_inv_cursor_frame = 0u;
    s_inv_cursor_timer = 0u;
    inv_draw_items_all();  /* icones statiques (1 fois) */
    inv_draw_cursor();     /* curseur actif par-dessus */
}

static void inventory_update(void)
{
    if (s_inv_phase == INV_PHASE_ACTION) {
        /* B / OPTION : ferme l'inventaire, retour au menu pause. */
        if (ngpc_pad_pressed & (PAD_B | PAD_OPTION)) {
            s_state = STATE_PAUSE;
            return;
        }
        if (ngpc_pad_pressed & PAD_UP) {
            if (s_inv_action > 0u) s_inv_action--;
        } else if (ngpc_pad_pressed & PAD_DOWN) {
            if (s_inv_action < (u8)(INV_ACT_COUNT - 1u)) s_inv_action++;
        }
        if (ngpc_pad_pressed & PAD_A) {
            /* Le cadre selecteur prend le relais sur le 1er slot.
             * Masque la fleche de nav (slots OAM disjoints du cadre). */
            ngpc_mspr_hide((u8)INV_NAV_SPR_BASE, INV_NAV_SPR_COUNT);
            s_inv_phase = INV_PHASE_ITEM;
            s_inv_item = 0u;
        }
    } else {  /* INV_PHASE_ITEM */
        if (ngpc_pad_pressed & PAD_B) {
            /* Annule la selection d'item, retour aux actions (masque le cadre). */
            ngpc_mspr_hide((u8)INV_ITEM_SPR_BASE, INV_ITEM_SPR_COUNT);
            s_inv_phase = INV_PHASE_ACTION;
        } else if (ngpc_pad_pressed & PAD_OPTION) {
            /* OPTION ferme directement tout l'inventaire. */
            s_state = STATE_PAUSE;
            return;
        } else {
            u8 col = (u8)(s_inv_item % INV_ITEM_COLS);
            u8 row = (u8)(s_inv_item / INV_ITEM_COLS);
            if (ngpc_pad_pressed & PAD_LEFT) {
                if (col > 0u) col--;
            } else if (ngpc_pad_pressed & PAD_RIGHT) {
                if (col < (u8)(INV_ITEM_COLS - 1u)) col++;
            } else if (ngpc_pad_pressed & PAD_UP) {
                if (row > 0u) row--;
            } else if (ngpc_pad_pressed & PAD_DOWN) {
                if (row < (u8)(INV_ITEM_ROWS - 1u)) row++;
            }
            s_inv_item = (u8)(row * INV_ITEM_COLS + col);

            if (ngpc_pad_pressed & PAD_A) {
                inventory_apply_action(s_inv_action, s_inv_item);
                /* Apres validation : retour a la selection d'action
                 * (masque le cadre selecteur). */
                ngpc_mspr_hide((u8)INV_ITEM_SPR_BASE, INV_ITEM_SPR_COUNT);
                s_inv_phase = INV_PHASE_ACTION;
            }
        }
    }

    /* Anim 2-frame du curseur fleche (visible uniquement en PHASE_ACTION). */
    s_inv_cursor_timer++;
    if (s_inv_cursor_timer >= INV_CURSOR_ANIM_TICKS) {
        s_inv_cursor_timer = 0u;
        s_inv_cursor_frame ^= 1u;
    }
    /* Seul le curseur actif est redessine chaque frame (slots OAM fixes) ->
     * pas de hide_all/icones par frame -> pas de flicker rangee du haut. */
    inv_draw_cursor();
}


/* =========================================================================
 * MKD-8 v2 : State Options - Editeur de seed donjon
 * =========================================================================
 * Affiche la seed courante (g_dungeon_seed). User peut la modifier :
 *   UP    : +1            DOWN  : -1
 *   RIGHT : +10           LEFT  : -10
 *   A     : set to 0 (= mode auto, ngpc_qrandom a chaque cluster)
 *   B     : retour STATE_MENU
 * 0 = auto/random (affiche "AUTO" au lieu d'un chiffre).
 * !=0 = seed forcee, partageable, cluster reproductible.
 * ========================================================================= */

static void options_draw_seed(void)
{
    /* Tout l'ecran Options utilise la variante font opaque (bg noir + letter blanc). */
    hud_text_print(GFX_SCR2, 5u, 4u, "OPTIONS");
    hud_text_print(GFX_SCR2, 3u, 8u, "SEED:");

    if (g_dungeon_seed == 0u) {
        hud_text_print(GFX_SCR2, 9u, 8u, "AUTO ");
    } else {
        hud_text_print(GFX_SCR2, 9u, 8u, "    ");  /* clear */
        hud_text_print_num(GFX_SCR2, 9u, 8u, g_dungeon_seed, 3u);
    }

    hud_text_print(GFX_SCR2, 2u, 12u, "UP/DOWN: +-1");
    hud_text_print(GFX_SCR2, 2u, 13u, "LFT/RGT: +-10");
    hud_text_print(GFX_SCR2, 2u, 14u, "A: AUTO  B: BACK");
}

static void options_init(void)
{
    hud_raster_disable();
    ngpc_gfx_set_viewport(0, 0, SCREEN_W, SCREEN_H);
    HW_SCR_PRIO = 0x00;
    ngpc_gfx_scroll(GFX_SCR1, 0, 0);
    ngpc_gfx_scroll(GFX_SCR2, 0, 0);
    ngpc_gfx_clear(GFX_SCR1);
    ngpc_gfx_clear(GFX_SCR2);
    ngpc_sprite_hide_all();
    ngpc_gfx_set_bg_color(RGB(0, 0, 0));

    /* Re-installe la police ahchay : standard + variante opaque (HUD). */
    ahchay_font_load();
    ahchay_font_set_palette(GFX_SCR2, 0u);
    hud_font_install_opaque_variant();
    hud_font_install_palettes(GFX_SCR2);

    options_draw_seed();
}

static void options_update(void)
{
    u8 redraw = 0u;

    if (ngpc_pad_pressed & PAD_B) {
        s_state = STATE_MENU;
        return;
    }

    if (ngpc_pad_pressed & PAD_UP) {
        g_dungeon_seed = (u8)(g_dungeon_seed + 1u);
        redraw = 1u;
    } else if (ngpc_pad_pressed & PAD_DOWN) {
        g_dungeon_seed = (u8)(g_dungeon_seed - 1u);
        redraw = 1u;
    } else if (ngpc_pad_pressed & PAD_RIGHT) {
        g_dungeon_seed = (u8)(g_dungeon_seed + 10u);
        redraw = 1u;
    } else if (ngpc_pad_pressed & PAD_LEFT) {
        g_dungeon_seed = (u8)(g_dungeon_seed - 10u);
        redraw = 1u;
    } else if (ngpc_pad_pressed & PAD_A) {
        g_dungeon_seed = 0u;  /* AUTO */
        redraw = 1u;
    }

    if (redraw) options_draw_seed();
}


/* =========================================================================
 * State: Dungeon (salles de la banque statique)
 * =========================================================================
 * Flow minimal : salle_01 -> porte N -> STATE_DUNGEON (bank room 0).
 * Dans le dungeon : mouvement cardinal metatile par metatile (pas d'anim
 * intermediaire pour l'instant). Collision via static_room_collision_at.
 * Camera scroll centree sur le joueur et clampee aux dims de la salle.
 * Transitions :
 *   - porte quelconque       -> room suivante du bank (cycle debug v1)
 *   - escalier               -> room suivante du bank (cluster v2)
 *   - trou (void drop)       -> idem + degats (TODO slice suivante)
 *   - PAD_B dans salle N     -> retour salle_01 porte sud
 *   - PAD_OPTION             -> STATE_PAUSE
 */

#define DUNGEON_SCREEN_W  160
/* Hauteur de la zone visible donjon : ecran 152 - 16 (HUD) = 136.
 * Camera clampe pour que la rangee la plus basse de la salle s'aligne
 * juste au-dessus du bandeau HUD (jamais cachee dessous). */
#define DUNGEON_SCREEN_H  136

static u8  s_dung_room_idx = 0u;
static u8  s_dung_gx       = 0u;
static u8  s_dung_gy       = 0u;
static s16 s_dung_px       = 0;
static s16 s_dung_py       = 0;
static s16 s_dung_cam_x    = 0;
static s16 s_dung_cam_y    = 0;

/* ---- Dungeon enemy runtime ----
 * Jusqu'a 3 enemies actifs par salle. OAM allocation pour le combat
 * salle_01-style (selector + attack effect partagent le meme slot,
 * mutuellement exclusifs en temps) :
 *   0..3   : SELECTOR ou ATTACK_EFFECT (shared, jamais simultanes)
 *   4..7   : enemy 0
 *   8..11  : player overlay
 *   12..15 : player base
 *   16..19 : enemy 1
 *   20..23 : enemy 2
 *   24..63 : HUD (40 OAM)
 * Total = 64 OAM exactement.
 */
#define DUNG_MAX_ENEMIES 3u
#define DUNG_FX_SPR_BASE      0u  /* selector OU attack effect */
#define DUNG_ENEMY_SPR_BASE_0 4u
#define DUNG_ENEMY_SPR_BASE_1 16u
#define DUNG_ENEMY_SPR_BASE_2 20u
/* MKD-chest : coffre 16x16 (4 OAM). Plage 24..63 libre depuis l'abandon du
 * bandeau HUD (HP_TEXT @24, skull_death @60). */
#define DUNG_CHEST_SPR_BASE   32u

/* MKD-chest : forward decls (utilises par dungeon_enemy_can_enter, avant def). */
static u8 dungeon_chest_at(s8 gx, s8 gy);

typedef struct {
    u8 alive;
    u8 type;            /* EnemyType */
    u8 hp;
    u8 gx;
    u8 gy;
    PlayerDirection dir;
    s8 mv_dx;           /* Skull linear axis */
    s8 mv_dy;
    u8 anim_frame;      /* 0 ou 1 */
    u8 anim_timer;
} DungeonEnemy;

static DungeonEnemy s_dung_enemies[DUNG_MAX_ENEMIES];
static u8 s_dung_enemy_count = 0u;

/* ---- Dungeon turn state machine (mirror salle_01) ----
 * WAIT_INPUT : on attend l'input du player
 * ATTACK_EFFECT : sprite d'effet d'attaque qui joue
 * DAMAGE_POPUP : popup texte du degat
 * ENEMY_TURNS : on iter sur les enemies (instantane = un seul tick)
 */
typedef enum {
    DUNG_TURN_WAIT_INPUT = 0,
    DUNG_TURN_ATTACK_EFFECT,
    DUNG_TURN_DAMAGE_POPUP,
    DUNG_TURN_ENEMY_TURNS,
    DUNG_TURN_CHEST_MSG    /* MKD-chest : message de loot affiche sur le HUD */
} DungeonTurnState;

/* MKD-chest : duree d'affichage du message de loot (frames, ~60/s), dismiss A/B. */
#define DUNG_CHEST_MSG_TICKS 120u
static u8 s_dung_chest_msg_timer = 0u;
static u8 s_dung_chest_msg_item  = 0u;  /* item recu (0 = sac plein) */

static DungeonTurnState s_dung_turn_state = DUNG_TURN_WAIT_INPUT;

/* Selector (cible designee par mouvement vers enemy). */
static u8 s_dung_sel_visible = 0u;
static u8 s_dung_sel_gx = 0u;
static u8 s_dung_sel_gy = 0u;

/* Attack effect (sprite) anchored a une cellule grid. */
static u8 s_dung_atk_visible = 0u;
static u8 s_dung_atk_gx = 0u;
static u8 s_dung_atk_gy = 0u;
static u8 s_dung_atk_timer = 0u;
static u8 s_dung_atk_visual = 0u;          /* NORMAL ou CRIT */
static AttackEffectSource s_dung_atk_source = ATTACK_EFFECT_NONE;

/* Damage popup (texte SCR2) anchored a une cellule grid. */
static u8 s_dung_popup_visible = 0u;
static u8 s_dung_popup_gx = 0u;
static u8 s_dung_popup_gy = 0u;
static u8 s_dung_popup_value = 0u;
static u8 s_dung_popup_timer = 0u;
static AttackEffectSource s_dung_popup_source = ATTACK_EFFECT_NONE;
static u8 s_dung_popup_is_fatal = 0u;

/* Index de l'enemy en train de jouer son tour (DUNG_TURN_ENEMY_TURNS).
 * 0..s_dung_enemy_count-1 ; >= count = tous joues, retour au wait_input. */
static u8 s_dung_turn_iter = 0u;

/* ---- Cluster system : 2-5 salles persistantes connectees par portes ----
 *
 * Chaque slot du cluster memorise bank_idx + voisins (N/E/S/W) ET
 * l'etat des actors a l'interieur (HP enemies, items pickup, etc.).
 * Quand le joueur passe une porte, on lookup le voisin -> change de
 * slot courant SANS regenerer la salle. Quand on revient, l'etat est
 * exactement comme on l'a laisse : enemy mort reste mort, item ramasse
 * reste ramasse, etc. (= persistance totale intra-cluster).
 *
 * Sortie cluster :
 *   - STAIR cell -> regen cluster suivant (escalier dans la salle)
 *   - VOID drop  -> regen cluster + degat joueur (TODO)
 *   - Porte sans voisin (= STAIR au bord du cluster) -> regen aussi
 *
 * Direction encoding (d) :
 *   0 = N (bit STATIC_ROOM_EXIT_N = 0x01)
 *   1 = E (bit 0x02)
 *   2 = S (bit 0x04)
 *   3 = W (bit 0x08)
 * Opposite = d ^ 2.
 *
 * Valeurs speciales pour neighbors[d] :
 *   < CLUSTER_MAX_ROOMS = slot d'une autre salle du cluster (porte interne)
 *   CLUSTER_NEIGHBOR_STAIR = porte sortante (= sortie du cluster)
 *   CLUSTER_NEIGHBOR_NONE = pas de porte de ce cote (mur plein) */
#define CLUSTER_MAX_ROOMS      5u
#define CLUSTER_MIN_ROOMS      2u
#define CLUSTER_NEIGHBOR_NONE  0xFFu
#define CLUSTER_NEIGHBOR_STAIR 0xFEu

/* Future : persistance par salle. On reserve des slots pour les
 * actors, populated quand on aura le systeme multi-enemy + props. */
#define CLUSTER_MAX_ACTORS_PER_ROOM 3u

typedef struct {
    u8 alive;        /* 0 = mort/picke, 1 = vivant/present */
    u8 hp;
    u8 type_id;      /* EnemyType (SLIME/SKULL/FLAMME/HENT) */
    u8 gx;
    u8 gy;
    u8 dir;          /* PlayerDirection persistant pour anim continue */
    s8 mv_dx;        /* Skull : axe de marche (-1/0/+1) */
    s8 mv_dy;
} ClusterActor;

typedef struct {
    u8 bank_idx;          /* index dans static_room_bank */
    u8 neighbors[4];      /* [N, E, S, W] -> slot ou STAIR/NONE */
    /* Etat actors persistants (vide tant que multi-enemy pas branche). */
    u8 actor_count;
    ClusterActor actors[CLUSTER_MAX_ACTORS_PER_ROOM];
    /* MKD-lock : porte verrouillee + declencheur (persistant entre passages).
     * lock_initialized = 0 : pas encore decide pour cette room (lazy roll
     *                        au premier enter).
     * lock_dir = 0 : pas de lock dans cette room (decide).
     * Sinon : porte verrouillee dans direction lock_dir, declencheur a
     *         (lock_tx, lock_ty), frame visible courante = lock_frame,
     *         held = 1 si quelque chose tenait le trigger en sortant. */
    u8 lock_initialized;
    u8 lock_dir;
    u8 lock_tx;
    u8 lock_ty;
    u8 lock_frame;
    u8 lock_held;
    u8 lock_held_by_type;  /* PUSHABLE_TYPE_* si une caisse/vase sur trigger,
                            * sinon NONE (player ou enemy : re-eval a l'entree) */
    /* MKD-chest : coffre dans la salle (0-2 par cluster, repartis sur des
     * salles distinctes). 1 max par salle. Ouvert -> chest_present=0 (persiste
     * entre passages). */
    u8 chest_present;      /* 1 = coffre non ouvert present */
    u8 chest_gx;
    u8 chest_gy;
} ClusterRoom;

static ClusterRoom s_cluster[CLUSTER_MAX_ROOMS];
static u8 s_cluster_count   = 0u;
static u8 s_cluster_current = 0u;

/* Mapping direction d -> bit STATIC_ROOM_EXIT_*. */
static const u8 CLUSTER_DIR_BIT[4] = {
    STATIC_ROOM_EXIT_N,  /* d=0 */
    STATIC_ROOM_EXIT_E,  /* d=1 */
    STATIC_ROOM_EXIT_S,  /* d=2 */
    STATIC_ROOM_EXIT_W,  /* d=3 */
};

/* Mapping direction de sortie d -> entry_side dans la salle suivante. */
static const u8 CLUSTER_DIR_TO_ENTRY[4] = {
    STATIC_ROOM_ENTRY_SOUTH, /* sort par N -> entre par S */
    STATIC_ROOM_ENTRY_WEST,  /* sort par E -> entre par W */
    STATIC_ROOM_ENTRY_NORTH, /* sort par S -> entre par N */
    STATIC_ROOM_ENTRY_EAST,  /* sort par W -> entre par E */
};

static void dungeon_enter_room(u8 room_idx, u8 entry_side);
static void cluster_generate(void);

static void dungeon_load_vram(void)
{
    /* Viewport plein ecran : la window NGPC clippe AUSSI les sprites
     * (verifie hardware) -> si on restreint a 136, le HUD disparait.
     * Ce qui empeche les murs bas d'etre caches sous le HUD c'est le
     * clamp camera (DUNGEON_SCREEN_H=136), pas le viewport. */
    ngpc_gfx_set_viewport(0, 0, SCREEN_W, SCREEN_H);
    HW_SCR_PRIO = 0x80; /* SCR2 devant : decors overlay vase/totem */
    ngpc_gfx_scroll(GFX_SCR1, 0, 0);
    ngpc_gfx_scroll(GFX_SCR2, 0, 0);
    ngpc_gfx_clear(GFX_SCR1);
    ngpc_gfx_clear(GFX_SCR2);
    ngpc_sprite_hide_all();
    ngpc_gfx_set_bg_color(RGB(0, 0, 0));

    /* Tiles bank (charge tileset_unit + ecrit PAL_WALL=0 olive, PAL_FLOOR=1
     * gris, PAL_DECO=1 sur SCR2). */
    static_room_loader_init_vram();

    /* Sprites joueur. */
    NGP_TILEMAP_LOAD_TILES_VRAM(player_topdown, player_topdown_tile_base);
    player_load_palettes();

    /* Sprites enemies dungeon (4 types : slime/skull/flamme/hent). */
    NGP_TILEMAP_LOAD_TILES_VRAM(lime_sheet, lime_sheet_tile_base);
    NGP_TILEMAP_LOAD_TILES_VRAM(skull, skull_tile_base);
    NGP_TILEMAP_LOAD_TILES_VRAM(flamme, flamme_tile_base);
    NGP_TILEMAP_LOAD_TILES_VRAM(hent, hent_tile_base);
    enemy_load_palettes();
    skull_load_palettes();
    flamme_load_palettes();
    hent_load_palettes();

    /* Selector + attack effect (combat salle_01-style). */
    NGP_TILEMAP_LOAD_TILES_VRAM(selecteur, selecteur_tile_base);
    NGP_TILEMAP_LOAD_TILES_VRAM(effect_attaque, effect_attaque_tile_base);
    NGP_TILEMAP_LOAD_TILES_VRAM(effect_attaque_crit, effect_attaque_crit_tile_base);
    selector_load_palettes();
    attack_effect_load_palettes();
    attack_effect_crit_load_palettes();

    /* skull_death : asset dedie au popup de mort fatale (different de
     * l'enemy "skull" charge plus haut). 4 tiles 16x16. */
    NGP_TILEMAP_LOAD_TILES_VRAM(skull_death, skull_death_tile_base);
    skull_death_load_palettes();

    /* MKD-chest : coffre (loot donjon). tiles 226-229, pal SPR 11. */
    {
        u8 i;
        u16 src;
        NGP_TILEMAP_LOAD_TILES_VRAM(coffre, coffre_tile_base);
        for (i = 0u; i < coffre_palette_count; i++) {
            src = (u16)i * 4u;
            ngpc_gfx_set_palette(GFX_SPR, (u8)(coffre_pal_base + i),
                coffre_palettes[src],
                coffre_palettes[(u16)(src + 1u)],
                coffre_palettes[(u16)(src + 2u)],
                coffre_palettes[(u16)(src + 3u)]);
        }
    }

    /* Le split HUD v4 est arme chaque frame par le main loop apres ngpc_vsync,
     * pilote par s_dung_hud_split_armed (set par hud_raster_update). */

    /* Le HUD bandeau ("PV:") est peint par dungeon_enter_room APRES
     * static_room_load() -- sinon le clear SCR2 de static_room_load
     * efface le label. */

    /* Police texte SCR2 pal=0 = installee par static_room_loader_init_vram()
     * (slot 0 de s_pal_scr2 = palette font ahchay, ecrite via array+loop
     * qui contourne le bug cc900 "4 RGB() immediates"). On NE rappelle
     * PAS ahchay_font_set_palette() ici : il ECRASERAIT avec garbage. */

    /* MKD-misc : reinstaller la font opaque (slot 416..511) car ecrasee
     * par menu_kuroi_dokutsu (176..500) pendant les transitions de state. */
    hud_font_install_opaque_variant();
    hud_font_install_palettes(GFX_SCR2);

    hud_load_vram();
}


static void dungeon_update_camera(void)
{
    s16 max_x;
    s16 max_y;
    s16 tx;
    s16 ty;

    max_x = (s16)((s16)static_room_w() * 16 - DUNGEON_SCREEN_W);
    max_y = (s16)((s16)static_room_h() * 16 - DUNGEON_SCREEN_H);
    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;

    tx = (s16)(s_dung_px - DUNGEON_SCREEN_W / 2);
    ty = (s16)(s_dung_py - DUNGEON_SCREEN_H / 2);
    if (tx < 0) tx = 0;
    if (tx > max_x) tx = max_x;
    if (ty < 0) ty = 0;
    if (ty > max_y) ty = max_y;

    s_dung_cam_x = tx;
    s_dung_cam_y = ty;

    /* Shadow scroll : l'ISR VBlank custom pushera ces valeurs vers les
     * registres HW au tout debut du VBlank, donc latche par K2GE des
     * scanline 0. Pas d'ecriture directe HW ici (le main loop est trop
     * long et le scroll serait change en plein milieu de la frame). */
    s_dung_scroll_scr1_x = (u8)s_dung_cam_x;
    s_dung_scroll_scr1_y = (u8)s_dung_cam_y;
    s_dung_scroll_scr2_x = (u8)s_dung_cam_x;
    s_dung_scroll_scr2_y = (u8)s_dung_cam_y;
    hud_raster_update();
}

static void dungeon_draw_player(void)
{
    s16 sx;
    s16 sy;
    u8 frame;

    sx = (s16)(s_dung_px - s_dung_cam_x);
    sy = (s16)(s_dung_py - s_dung_cam_y);
    frame = (u8)((u8)s_player_dir * 3u + PLAYER_IDLE_FRAME);

    /* SPR_MIDDLE = entre SCR1 (room) et SCR2 (HUD + decor + popups).
     * Le HUD couvre les sprites en bas, decor BG passe en avant-plan. */
    ngpc_mspr_draw((u8)PLAYER_SPR_BASE, sx, sy,
        s_player_base_frames[frame], (u8)SPR_MIDDLE);
    ngpc_mspr_draw((u8)PLAYER_OVERLAY_SPR_BASE, sx, sy,
        s_player_overlay_frames[frame], (u8)SPR_MIDDLE);
}

/* MKD-chest : dessine le coffre de la salle courante (sprite 16x16 en world
 * coords camera-relative, SPR_MIDDLE comme joueur/enemies). Pas de coffre ->
 * masque les 4 OAM (gere la disparition apres ouverture). */
static void dungeon_draw_chest(void)
{
    s16 sx, sy;
    ClusterRoom *cr;

    if (s_cluster_current >= CLUSTER_MAX_ROOMS) {
        ngpc_mspr_hide((u8)DUNG_CHEST_SPR_BASE, 4u);
        return;
    }
    cr = &s_cluster[s_cluster_current];
    if (!cr->chest_present) {
        ngpc_mspr_hide((u8)DUNG_CHEST_SPR_BASE, 4u);
        return;
    }
    sx = (s16)((s16)((s16)cr->chest_gx * 16) - s_dung_cam_x);
    sy = (s16)((s16)((s16)cr->chest_gy * 16) - s_dung_cam_y);
    ngpc_mspr_draw((u8)DUNG_CHEST_SPR_BASE, sx, sy, &coffre_frame_0, (u8)SPR_MIDDLE);
}

/* MKD-lock : sauvegarde l'etat anim de la porte verrouillee dans la slot
 * cluster donnee (avant transition vers une autre room). lock_dir/tx/ty
 * sont deja persistes par dungeon_apply_lock_for_current_room ; on ne
 * sauve ici que l'etat dynamique (frame + held + type qui tient).
 *
 * lock_held_by_type est lu depuis le pushable a la position trigger si
 * un pushable y est ; sinon NONE (player ou enemy, etat non persistant
 * cote pushable -- ils sont restaures par leur propre systeme). */
static void dungeon_save_lock_to_cluster(u8 slot)
{
    if (slot >= CLUSTER_MAX_ROOMS) return;
    s_cluster[slot].lock_frame = static_room_lock_frame();
    s_cluster[slot].lock_held = static_room_lock_held();
    if (static_room_lock_dir() != 0u) {
        s_cluster[slot].lock_held_by_type = static_room_pushable_type_at(
            static_room_lock_trigger_x(), static_room_lock_trigger_y());
    } else {
        s_cluster[slot].lock_held_by_type = (u8)PUSHABLE_TYPE_NONE;
    }
}

/* MKD-lock : decide (1er passage) ou applique (passages suivants) l'etat
 * lock+trigger pour la salle correspondant a s_cluster_current. A appeler
 * APRES static_room_load (qui a roule le furnishing -> les decor_anchors
 * libres sont stabilisees). */
static void dungeon_apply_lock_for_current_room(void)
{
    ClusterRoom *cr = &s_cluster[s_cluster_current];
    const StaticRoomDef *def = static_room_current();
    u8 mask;
    u8 picks[4];
    u8 count;
    u8 i;
    u8 free_n;

    if (!cr->lock_initialized) {
        cr->lock_initialized = 1u;
        cr->lock_dir = 0u;
        cr->lock_held = 0u;
        cr->lock_frame = (u8)LOCK_DOOR_FRAME_CLOSED;

        /* Roll ~25% pour un lock dans cette room. Conditions :
         *   - room a un exit utile (neighbor cluster valide)
         *   - room a au moins une decor_anchor libre (pour le trigger)
         *   - room a au moins UN pushable (vase/caisse) dispo pour bloquer
         *     le trigger pendant la traversee de la porte. Sinon le player
         *     doit tenir le trigger lui-meme mais en sortant la porte se
         *     referme = softlock (fix 2026-05-17). */
        if (def != 0 && def->exits_mask != 0u &&
            static_room_pushable_count() > 0u) {
            free_n = static_room_free_anchor_count();
            if (free_n > 0u && cluster_rng_u8() < 64u) {
                /* Sigma des exits utiles : exits_mask intersected avec
                 * neighbors[d] in cluster slot. */
                mask = def->exits_mask;
                count = 0u;
                if (mask & STATIC_ROOM_EXIT_N) {
                    if (cr->neighbors[0] < CLUSTER_MAX_ROOMS) picks[count++] = STATIC_ROOM_EXIT_N;
                }
                if (mask & STATIC_ROOM_EXIT_E) {
                    if (cr->neighbors[1] < CLUSTER_MAX_ROOMS) picks[count++] = STATIC_ROOM_EXIT_E;
                }
                if (mask & STATIC_ROOM_EXIT_S) {
                    if (cr->neighbors[2] < CLUSTER_MAX_ROOMS) picks[count++] = STATIC_ROOM_EXIT_S;
                }
                if (mask & STATIC_ROOM_EXIT_W) {
                    if (cr->neighbors[3] < CLUSTER_MAX_ROOMS) picks[count++] = STATIC_ROOM_EXIT_W;
                }
                if (count > 0u) {
                    cr->lock_dir = picks[cluster_rng_u8() % count];
                    {
                        u8 ax = 0u, ay = 0u;
                        u8 idx = (u8)(cluster_rng_u8() % free_n);
                        if (static_room_get_free_anchor(idx, &ax, &ay)) {
                            cr->lock_tx = ax;
                            cr->lock_ty = ay;
                        } else {
                            /* Cas defensif : annule le lock. */
                            cr->lock_dir = 0u;
                        }
                    }
                }
            }
        }
    }

    /* Apply state au loader. lock_dir=0 -> static_room_lock_init met juste
     * a zero (pas de dessin). */
    static_room_lock_init(cr->lock_dir, cr->lock_tx, cr->lock_ty,
                          cr->lock_frame, cr->lock_held);

    /* Si a la sortie precedente le trigger etait tenu par un pushable
     * (caisse/vase), le replacer sur le trigger. Le furnishing re-roule
     * a static_room_load place les pushables a leur position initiale
     * (deterministe par seed), donc sans cette restauration la caisse
     * posee sur le trigger serait perdue entre passages. */
    if (cr->lock_dir != 0u && cr->lock_held_by_type != (u8)PUSHABLE_TYPE_NONE) {
        static_room_pushable_add_at(cr->lock_tx, cr->lock_ty,
                                    cr->lock_held_by_type);
    }
}

static void dungeon_enter_room(u8 room_idx, u8 entry_side)
{
    u8 gx;
    u8 gy;

    dungeon_load_vram();

    s_dung_room_idx = room_idx;
    static_room_load(room_idx);
    /* MKD-5 v3 : scelle les portes qui ne menent a aucun neighbor cluster.
     * Une porte vers STAIR/NONE est remplacee visuellement par un mur et
     * sa cellule devient SOLID (cf static_room_loader.c). Une porte n'agit
     * jamais comme stair : seuls les stair_socket/void tiles permettent
     * de changer de cluster (cf dungeon_try_move). */
    {
        const StaticRoomDef *def_cur = static_room_bank_get(room_idx);
        u8 seal = 0u;
        u8 d;
        if (def_cur != 0) {
            for (d = 0u; d < 4u; d++) {
                u8 dir_bit = CLUSTER_DIR_BIT[d];
                if (!(def_cur->exits_mask & dir_bit)) continue;
                {
                    u8 nb = s_cluster[s_cluster_current].neighbors[d];
                    if (nb >= CLUSTER_MAX_ROOMS) seal |= dir_bit;
                }
            }
        }
        static_room_seal_doors(seal);
    }
    /* MKD-lock : applique l'etat lock+trigger pour la salle. Lazy-roll
     * au premier passage, lecture cluster slot apres. APRES seal_doors
     * (pour eviter de lock une porte qui serait sealed) et APRES
     * static_room_load (free_anchor_count valide). */
    dungeon_apply_lock_for_current_room();
    /* HUD bandeau : peint APRES static_room_load (qui clear SCR2 et y
     * pose les decos vase/totem aux world coords). Tiles "PV:" au
     * tilemap SCR2 tile_y=17 ; le raster split SCR2 garde la zone
     * lignes 136..151 figee a (0, 0). */
    hud_paint_bg();
    static_room_entry_position(entry_side, &gx, &gy);

    s_dung_gx = gx;
    s_dung_gy = gy;
    s_dung_px = (s16)((s16)gx * 16);
    s_dung_py = (s16)((s16)gy * 16);

    /* Reset combat state pour la nouvelle salle. */
    s_dung_turn_state = DUNG_TURN_WAIT_INPUT;
    s_dung_turn_iter = 0u;
    dung_sel_hide();
    dung_atk_hide();
    s_dung_popup_visible = 0u;
    s_dung_popup_timer = 0u;

    /* Charge les enemies persistants du cluster slot courant. */
    dungeon_hide_enemy_sprites();
    dungeon_load_enemies_from_cluster(s_cluster_current);

    dungeon_update_camera();
    dungeon_draw_enemies();
    dungeon_draw_player();
    dungeon_draw_chest();
    hud_draw();
}

static void dungeon_init(void)
{
    /* Genere le cluster initial (3-5 salles connectees, persistant
     * jusqu'a sortie via stair/void). Spawn dans une room aleatoire du
     * cluster (pas force au centre slot 0). Entry direction NONE -> spawn
     * au centre de la room. */
    cluster_generate();
    if (s_cluster_count > 0u) {
        s_cluster_current = (u8)(cluster_rng_u8() % s_cluster_count);
    } else {
        s_cluster_current = 0u;
    }

    /* MKD-depth : profondeur finale tiree par seed (deterministe via le
     * cluster RNG deja seede par cluster_generate). depth_current = 1
     * pour le cluster d'entree. Stats reset pour le run. */
    g_depth_target = (u8)(DEPTH_TARGET_MIN +
        (cluster_rng_u8() % (u8)(DEPTH_TARGET_MAX - DEPTH_TARGET_MIN + 1u)));
    g_depth_current = 1u;
    g_run_kills = 0u;
    g_run_turns = 0u;
    dungeon_enter_room(s_cluster[s_cluster_current].bank_idx,
        STATIC_ROOM_ENTRY_NONE);
}

/* Retour au donjon depuis PAUSE / MINIMAP / OPTIONS : restore VRAM + sprites
 * + room courante SANS regenerer le cluster ni replacer le joueur.
 *
 * BUGFIX 2026-05-16 : utilisait static_room_load() qui re-rolle le mobilier
 * (vases / caisses) a leur position INITIALE -> les caisses poussees par
 * le joueur revenaient a leur place apres pause. static_room_redraw_current()
 * redessine la salle a partir des arrays runtime conserves (pushables +
 * active_decor). */
static void dungeon_resume(void)
{
    dungeon_load_vram();
    static_room_redraw_current();
    /* MKD-5 v3 : reseal doors (le redraw a reset le mask). */
    {
        const StaticRoomDef *def_cur = static_room_bank_get(s_dung_room_idx);
        u8 seal = 0u;
        u8 d;
        if (def_cur != 0) {
            for (d = 0u; d < 4u; d++) {
                u8 dir_bit = CLUSTER_DIR_BIT[d];
                if (!(def_cur->exits_mask & dir_bit)) continue;
                {
                    u8 nb = s_cluster[s_cluster_current].neighbors[d];
                    if (nb >= CLUSTER_MAX_ROOMS) seal |= dir_bit;
                }
            }
        }
        static_room_seal_doors(seal);
    }
    hud_paint_bg();

    /* Reset combat state (selector/popup peuvent etre stale apres pause). */
    s_dung_turn_state = DUNG_TURN_WAIT_INPUT;
    s_dung_turn_iter = 0u;
    dung_sel_hide();
    dung_atk_hide();
    s_dung_popup_visible = 0u;
    s_dung_popup_timer = 0u;

    dungeon_hide_enemy_sprites();
    dungeon_load_enemies_from_cluster(s_cluster_current);

    dungeon_update_camera();
    dungeon_draw_enemies();
    dungeon_draw_player();
    dungeon_draw_chest();
    hud_draw();
}

/* Retourne le masque exit (N/S/E/W) si la case (nx, ny) est une porte
 * franchissable, ou 0 sinon. */
static u8 dungeon_exit_at(s8 nx, s8 ny)
{
    const StaticRoomDef *def;
    u8 mask;

    def = static_room_current();
    if (def == 0) return 0u;
    mask = def->exits_mask;

    if ((mask & STATIC_ROOM_EXIT_N) && ny == 0 &&
        ((u8)nx == def->door_col_lo || (u8)nx == def->door_col_hi)) {
        return STATIC_ROOM_EXIT_N;
    }
    if ((mask & STATIC_ROOM_EXIT_S) && ny == (s8)(def->h - 1u) &&
        ((u8)nx == def->door_col_lo || (u8)nx == def->door_col_hi)) {
        return STATIC_ROOM_EXIT_S;
    }
    if ((mask & STATIC_ROOM_EXIT_W) && nx == 0 &&
        ((u8)ny == def->door_row_lo || (u8)ny == def->door_row_hi)) {
        return STATIC_ROOM_EXIT_W;
    }
    if ((mask & STATIC_ROOM_EXIT_E) && nx == (s8)(def->w - 1u) &&
        ((u8)ny == def->door_row_lo || (u8)ny == def->door_row_hi)) {
        return STATIC_ROOM_EXIT_E;
    }
    return 0u;
}

/* Cherche dans la bank une room qui possede l'exit demande (bit
 * STATIC_ROOM_EXIT_*). Iteration cyclique a partir de current+1 -> on
 * ne reprend pas la meme salle deux fois de suite. Renvoie 0 (room par
 * defaut) si aucune match (cas pathologique). */
static u8 dungeon_pick_room_with_exit(u8 required_exit)
{
    u8 i;
    u8 idx;
    const StaticRoomDef *def;

    idx = (u8)((s_dung_room_idx + 1u) % STATIC_ROOM_BANK_COUNT);
    for (i = 0u; i < STATIC_ROOM_BANK_COUNT; i++) {
        def = static_room_bank_get(idx);
        if (def != 0 && (def->exits_mask & required_exit) != 0u) {
            return idx;
        }
        idx = (u8)((idx + 1u) % STATIC_ROOM_BANK_COUNT);
    }
    return 0u;
}

/* Cherche n'importe quelle room avec au moins une exit (pour
 * stair/void = transition cluster, sans direction d'entree imposee).
 * Retourne aussi l'entry_side base sur la premiere exit trouvee de la
 * room choisie. */
static u8 dungeon_pick_room_any_exit(u8 *out_entry_side)
{
    u8 i;
    u8 idx;
    const StaticRoomDef *def;

    idx = (u8)((s_dung_room_idx + 1u) % STATIC_ROOM_BANK_COUNT);
    for (i = 0u; i < STATIC_ROOM_BANK_COUNT; i++) {
        def = static_room_bank_get(idx);
        if (def != 0 && def->exits_mask != 0u) {
            /* Spawn le joueur a la premiere exit dispo, dans l'ordre N/S/W/E. */
            if      (def->exits_mask & STATIC_ROOM_EXIT_N) *out_entry_side = STATIC_ROOM_ENTRY_NORTH;
            else if (def->exits_mask & STATIC_ROOM_EXIT_S) *out_entry_side = STATIC_ROOM_ENTRY_SOUTH;
            else if (def->exits_mask & STATIC_ROOM_EXIT_W) *out_entry_side = STATIC_ROOM_ENTRY_WEST;
            else                                            *out_entry_side = STATIC_ROOM_ENTRY_EAST;
            return idx;
        }
        idx = (u8)((idx + 1u) % STATIC_ROOM_BANK_COUNT);
    }
    *out_entry_side = STATIC_ROOM_ENTRY_NONE;
    return 0u;
}

/* ---- Cluster generation + lookup ---- */

/* Verifie si bank_idx est deja utilise par un slot rempli du cluster.
 * INVALID_INDEX exclu pour gerer les slots non encore initialises. */
static u8 cluster_bank_is_used(u8 bank_idx)
{
    u8 i;
    for (i = 0u; i < s_cluster_count; i++) {
        if (s_cluster[i].bank_idx != STATIC_ROOM_BANK_INVALID_INDEX &&
            s_cluster[i].bank_idx == bank_idx) {
            return 1u;
        }
    }
    return 0u;
}

/* Retourne 1 si au moins une salle du cluster a un stair_socket
 * (= escalier visible) dans le bank. */
static u8 cluster_has_stair(void)
{
    u8 i;
    const StaticRoomDef *def;
    for (i = 0u; i < s_cluster_count; i++) {
        def = static_room_bank_get(s_cluster[i].bank_idx);
        if (def != 0 && def->stair_socket_count > 0u) {
            return 1u;
        }
    }
    return 0u;
}

/* Pick une bank room avec required_exit set, NON utilisee par le
 * cluster. Score (lower = better) :
 *   prefer_stair && stair_socket>0 : 0   (top priorite)
 *   stair_socket>0 (mais prefer=0) : 100 (penalite, eviter)
 *   sinon                          : 1+N (N = nombre d'exits, prefer leaf)
 * Fallback : premiere matching utilisee (sans penalite stair),
 * sinon 0. */
static u8 cluster_pick_unused_with_exit(u8 required_exit, u8 seed_offset)
{
    u8 i, idx, m;
    u8 best_idx = STATIC_ROOM_BANK_INVALID_INDEX;
    u8 best_score = 0xFFu;
    u8 fallback = STATIC_ROOM_BANK_INVALID_INDEX;
    u8 exit_count;
    u8 score;
    u8 prefer_stair;
    const StaticRoomDef *def;

    prefer_stair = (u8)(!cluster_has_stair());

    idx = (u8)((seed_offset + 1u) % STATIC_ROOM_BANK_COUNT);
    for (i = 0u; i < STATIC_ROOM_BANK_COUNT; i++) {
        def = static_room_bank_get(idx);
        if (def != 0 && (def->exits_mask & required_exit) != 0u) {
            exit_count = 0u;
            for (m = 0u; m < 4u; m++) {
                if (def->exits_mask & (u8)(1u << m)) exit_count++;
            }
            if (def->stair_socket_count > 0u) {
                score = prefer_stair ? 0u : 100u;
            } else {
                score = (u8)(1u + exit_count);
            }

            if (!cluster_bank_is_used(idx)) {
                if (score < best_score) {
                    best_score = score;
                    best_idx = idx;
                }
            } else if (fallback == STATIC_ROOM_BANK_INVALID_INDEX) {
                fallback = idx;
            }
        }
        idx = (u8)((idx + 1u) % STATIC_ROOM_BANK_COUNT);
    }

    if (best_idx != STATIC_ROOM_BANK_INVALID_INDEX) return best_idx;
    if (fallback != STATIC_ROOM_BANK_INVALID_INDEX) return fallback;
    return STATIC_ROOM_BANK_INVALID_INDEX;
}

/* Genere un cluster de 2-5 salles connectees a partir de zero. */
static void cluster_generate(void)
{
    u8 i, j, d, slot, new_slot, opp;
    u8 target;
    u8 seed;
    const StaticRoomDef *def;

    /* Reset complet : bank_idx = INVALID (pour ne pas confondre avec
     * bank room 0 lors du dedup), neighbors = NONE, actors = vides. */
    s_cluster_count   = 0u;
    s_cluster_current = 0u;
    for (i = 0u; i < CLUSTER_MAX_ROOMS; i++) {
        s_cluster[i].bank_idx = STATIC_ROOM_BANK_INVALID_INDEX;
        s_cluster[i].actor_count = 0u;
        for (d = 0u; d < 4u; d++) {
            s_cluster[i].neighbors[d] = CLUSTER_NEIGHBOR_NONE;
        }
        for (j = 0u; j < CLUSTER_MAX_ACTORS_PER_ROOM; j++) {
            s_cluster[i].actors[j].alive = 0u;
        }
        /* MKD-chest : pas de coffre tant que le pass 5 n'en assigne pas. */
        s_cluster[i].chest_present = 0u;
        /* MKD-lock : lazy init au premier enter. */
        s_cluster[i].lock_initialized = 0u;
        s_cluster[i].lock_dir = 0u;
        s_cluster[i].lock_tx = 0u;
        s_cluster[i].lock_ty = 0u;
        s_cluster[i].lock_frame = 0u;
        s_cluster[i].lock_held = 0u;
        s_cluster[i].lock_held_by_type = 0u;
    }

    /* MKD-8 : seed pilotable. Si g_dungeon_seed != 0 -> seed forcee par
     * l'user (menu Options) -> meme cluster reproductible. Sinon random. */
    if (g_dungeon_seed != 0u) {
        seed = g_dungeon_seed;
    } else {
        seed = ngpc_qrandom();
    }
    g_last_cluster_seed = seed;

    /* MKD-3-B-1 : seed le RNG local pour rendre TOUTE la generation
     * deterministique a partir de cette graine. Le combat (ngpc_qrandom)
     * reste random, seule la generation cluster + enemies + items l'est. */
    cluster_rng_seed((u16)seed | ((u16)seed << 8));

    /* Room 0 : pick une room sans stair_socket + >=2 exits (pour avoir des
     * branches possibles). Aucune priorite sur entrance_unique (retiree car
     * forcait des clusters en etoile). entrance_unique peut etre piquee si
     * elle qualifie (4 exits, pas de stair_socket). */
    {
        u8 j, idx;
        const StaticRoomDef *cand;
        u8 found = STATIC_ROOM_BANK_INVALID_INDEX;

        idx = (u8)((seed + 1u) % STATIC_ROOM_BANK_COUNT);
        for (j = 0u; j < STATIC_ROOM_BANK_COUNT; j++) {
            cand = static_room_bank_get(idx);
            if (cand != 0 && cand->stair_socket_count == 0u &&
                cand->exits_mask != 0u)
            {
                u8 ec = 0u, m;
                for (m = 0u; m < 4u; m++) {
                    if (cand->exits_mask & (u8)(1u << m)) ec++;
                }
                if (ec >= 2u) { found = idx; break; }
                if (found == STATIC_ROOM_BANK_INVALID_INDEX) found = idx;
            }
            idx = (u8)((idx + 1u) % STATIC_ROOM_BANK_COUNT);
        }

        if (found == STATIC_ROOM_BANK_INVALID_INDEX) {
            /* Fallback : any-exit. */
            u8 entry_unused;
            u8 saved = s_dung_room_idx;
            s_dung_room_idx = seed;
            found = dungeon_pick_room_any_exit(&entry_unused);
            s_dung_room_idx = saved;
        }
        s_cluster[0].bank_idx = found;
    }
    s_cluster_count = 1u;

    /* target 3..5 (pas de cluster a 2 salles, trop court). */
    target = (u8)(3u + (cluster_rng_u8() % 3u));
    if (target > CLUSTER_MAX_ROOMS) target = CLUSTER_MAX_ROOMS;
    if (target < 3u) target = 3u;

    /* Expansion avec 4 modes topologiques equiprobables (mode pioche au
     * debut, persiste pendant toute la generation) :
     *   STAR : BFS pur depuis front[0] -> slot 0 epuise toutes ses exits
     *   LINE : DFS pur depuis front[top] -> chaine deep
     *   T_SHAPE : 70% BFS + 30% DFS -> branche + tee
     *   L_SHAPE : 30% BFS + 70% DFS -> coude
     * Direction shuffling Fisher-Yates a chaque expansion. */
    {
        u8 front[CLUSTER_MAX_ROOMS];
        u8 front_count = 1u;
        u8 dirs[4];
        u8 di;
        u8 mode = (u8)(cluster_rng_u8() & 0x03u);
        u8 bfs_threshold;  /* strict: bfs if cluster_rng_u8() < threshold */
        if (mode == 0u)      bfs_threshold = 255u;  /* STAR    : ~100% BFS */
        else if (mode == 1u) bfs_threshold = 0u;    /* LINE    :   0% BFS = pure DFS */
        else if (mode == 2u) bfs_threshold = 178u;  /* T_SHAPE :  70% BFS */
        else                 bfs_threshold = 76u;   /* L_SHAPE :  30% BFS */
        front[0] = 0u;

        while (front_count > 0u && s_cluster_count < target) {
            u8 cur_idx, cur_slot;
            u8 expanded = 0u;

            /* Pick depuis front[] : BFS = front[0], DFS = front[top] */
            if (cluster_rng_u8() < bfs_threshold) cur_idx = 0u;
            else cur_idx = (u8)(front_count - 1u);
            cur_slot = front[cur_idx];

            def = static_room_bank_get(s_cluster[cur_slot].bank_idx);
            if (def == 0) {
                /* Retire cur_slot du front via shift */
                u8 ii;
                for (ii = cur_idx; ii + 1u < front_count; ii++)
                    front[ii] = front[ii + 1u];
                front_count--;
                continue;
            }

            /* Init + Fisher-Yates shuffle des 4 directions */
            dirs[0] = 0u; dirs[1] = 1u; dirs[2] = 2u; dirs[3] = 3u;
            for (di = 3u; di > 0u; di--) {
                u8 jj = (u8)(cluster_rng_u8() % (u8)(di + 1u));
                u8 tmp = dirs[di]; dirs[di] = dirs[jj]; dirs[jj] = tmp;
            }

            for (di = 0u; di < 4u; di++) {
                u8 new_bank;
                d = dirs[di];
                if (!(def->exits_mask & CLUSTER_DIR_BIT[d])) continue;
                if (s_cluster[cur_slot].neighbors[d] != CLUSTER_NEIGHBOR_NONE) continue;

                opp = (u8)(d ^ 2u);
                new_bank = cluster_pick_unused_with_exit(CLUSTER_DIR_BIT[opp],
                    (u8)(s_cluster[cur_slot].bank_idx + cluster_rng_u8()));
                if (new_bank == STATIC_ROOM_BANK_INVALID_INDEX) continue;

                new_slot = s_cluster_count;
                s_cluster[new_slot].bank_idx = new_bank;
                s_cluster[new_slot].neighbors[opp] = cur_slot;
                s_cluster[cur_slot].neighbors[d] = new_slot;
                s_cluster_count++;
                front[front_count++] = new_slot;
                expanded = 1u;
                break;
            }

            if (!expanded) {
                /* cur_slot ne peut plus etre etendu : retire-le du front */
                u8 ii;
                for (ii = cur_idx; ii + 1u < front_count; ii++)
                    front[ii] = front[ii + 1u];
                front_count--;
            }
        }
    }

    /* Pass 2 : densifier le graphe en linkant les exits non chainees
     * entre slots existants. Mais on NE LINK PAS deux slots qui sont
     * deja relies par une autre direction -> evite les double-links
     * confondants ou le joueur traverse 2 portes differentes du meme
     * slot et arrive a la meme salle voisine via des cotes opposes
     * (impression de "salle qui change"). */
    {
        u8 slot_a, slot_b, opp, mm;
        u8 already_linked;
        const StaticRoomDef *def_a;
        const StaticRoomDef *def_b;
        for (slot_a = 0u; slot_a < s_cluster_count; slot_a++) {
            def_a = static_room_bank_get(s_cluster[slot_a].bank_idx);
            if (def_a == 0) continue;
            for (d = 0u; d < 4u; d++) {
                if (!(def_a->exits_mask & CLUSTER_DIR_BIT[d])) continue;
                if (s_cluster[slot_a].neighbors[d] != CLUSTER_NEIGHBOR_NONE) continue;
                opp = (u8)(d ^ 2u);
                for (slot_b = 0u; slot_b < s_cluster_count; slot_b++) {
                    if (slot_b == slot_a) continue;
                    def_b = static_room_bank_get(s_cluster[slot_b].bank_idx);
                    if (def_b == 0) continue;
                    if (!(def_b->exits_mask & CLUSTER_DIR_BIT[opp])) continue;
                    if (s_cluster[slot_b].neighbors[opp] != CLUSTER_NEIGHBOR_NONE) continue;
                    /* Bloquer les double-links : si slot_a a deja
                     * un lien vers slot_b dans une autre direction,
                     * skip. */
                    already_linked = 0u;
                    for (mm = 0u; mm < 4u; mm++) {
                        if (s_cluster[slot_a].neighbors[mm] == slot_b) {
                            already_linked = 1u;
                            break;
                        }
                    }
                    if (already_linked) continue;
                    s_cluster[slot_a].neighbors[d] = slot_b;
                    s_cluster[slot_b].neighbors[opp] = slot_a;
                    break;
                }
            }
        }
    }

    /* Pass 3 : exits restantes non connectees -> STAIR (sortie cluster).
     * Garantit qu'au moins une exit ouverte mene hors du cluster (sinon
     * le joueur serait piege). */
    for (slot = 0u; slot < s_cluster_count; slot++) {
        def = static_room_bank_get(s_cluster[slot].bank_idx);
        if (def == 0) continue;
        for (d = 0u; d < 4u; d++) {
            if ((def->exits_mask & CLUSTER_DIR_BIT[d]) &&
                s_cluster[slot].neighbors[d] == CLUSTER_NEIGHBOR_NONE)
            {
                s_cluster[slot].neighbors[d] = CLUSTER_NEIGHBOR_STAIR;
            }
        }
    }

    /* Pass 4 : spawn enemies (0..3 par salle, types aleatoires). On
     * utilise les spawn-points predefinis dans la bank pour eviter les
     * murs/decors. Type tire dans ENEMY_TYPE_SLIME..ENEMY_TYPE_HENT. */
    for (slot = 0u; slot < s_cluster_count; slot++) {
        u8 i;
        u8 count;
        u8 max_avail;
        u8 spawn_idx;
        u8 used_mask;
        u8 type;
        u8 retries;
        u8 r;

        def = static_room_bank_get(s_cluster[slot].bank_idx);
        if (def == 0 || def->enemy_spawn_count == 0u) {
            s_cluster[slot].actor_count = 0u;
            continue;
        }

        max_avail = def->enemy_spawn_count;
        if (max_avail > CLUSTER_MAX_ACTORS_PER_ROOM)
            max_avail = CLUSTER_MAX_ACTORS_PER_ROOM;

        /* MKD-3-B-2 : count tire dans [role_min, role_max], clamp a max_avail.
         * ENTRY/SAFE -> 0 enemy, COMBAT_LIGHT -> 1-2, COMBAT_HEAVY -> 2-3, etc. */
        {
            u8 r_min = cluster_enemy_min_for_role(def->semantic_role);
            u8 r_max = cluster_enemy_max_for_role(def->semantic_role);
            if (r_max > max_avail) r_max = max_avail;
            if (r_min > r_max)     r_min = r_max;
            if (r_max > r_min) {
                count = (u8)(r_min + (cluster_rng_u8() % (u8)(r_max - r_min + 1u)));
            } else {
                count = r_min;
            }
        }

        used_mask = 0u;
        s_cluster[slot].actor_count = 0u;
        for (i = 0u; i < count; i++) {
            retries = 0u;
            do {
                spawn_idx = (u8)(cluster_rng_u8() % def->enemy_spawn_count);
                retries++;
            } while ((used_mask & (u8)(1u << spawn_idx)) != 0u && retries < 8u);
            used_mask = (u8)(used_mask | (u8)(1u << spawn_idx));

            type = (u8)(cluster_rng_u8() % (u8)ENEMY_TYPE_COUNT);

            s_cluster[slot].actors[i].alive = 1u;
            s_cluster[slot].actors[i].type_id = type;
            s_cluster[slot].actors[i].hp = g_enemy_stats[type].max_hp;
            s_cluster[slot].actors[i].gx = def->enemy_spawns[spawn_idx].x;
            s_cluster[slot].actors[i].gy = def->enemy_spawns[spawn_idx].y;
            s_cluster[slot].actors[i].dir = (u8)PLAYER_DIR_DOWN;

            if (type == (u8)ENEMY_TYPE_SKULL) {
                /* Direction lineaire initiale : H ou V, sens aleatoire. */
                r = (u8)(cluster_rng_u8() & 0x03u);
                if (r == 0u) {
                    s_cluster[slot].actors[i].mv_dx = 1;
                    s_cluster[slot].actors[i].mv_dy = 0;
                } else if (r == 1u) {
                    s_cluster[slot].actors[i].mv_dx = -1;
                    s_cluster[slot].actors[i].mv_dy = 0;
                } else if (r == 2u) {
                    s_cluster[slot].actors[i].mv_dx = 0;
                    s_cluster[slot].actors[i].mv_dy = 1;
                } else {
                    s_cluster[slot].actors[i].mv_dx = 0;
                    s_cluster[slot].actors[i].mv_dy = -1;
                }
            } else {
                s_cluster[slot].actors[i].mv_dx = 0;
                s_cluster[slot].actors[i].mv_dy = 0;
            }
            s_cluster[slot].actor_count++;
        }

        for (i = count; i < CLUSTER_MAX_ACTORS_PER_ROOM; i++) {
            s_cluster[slot].actors[i].alive = 0u;
        }
    }

    /* Pass 5 : coffres. 0-2 par cluster, places sur des salles DISTINCTES
     * ayant au moins un item_spawn socket (1 coffre max par salle). Contenu
     * tire a l'ouverture (cf dungeon_open_chest). */
    {
        u8 num_chests = (u8)(cluster_rng_u8() % 3u);  /* 0, 1 ou 2 */
        u8 placed = 0u;
        u8 attempts = 0u;
        u8 sl, si;
        const StaticRoomDef *cd;

        while (placed < num_chests && attempts < 32u) {
            attempts++;
            sl = (u8)(cluster_rng_u8() % s_cluster_count);
            cd = static_room_bank_get(s_cluster[sl].bank_idx);
            if (cd == 0 || cd->item_spawn_count == 0u) continue;
            if (s_cluster[sl].chest_present) continue;  /* deja un coffre ici */
            si = (u8)(cluster_rng_u8() % cd->item_spawn_count);
            s_cluster[sl].chest_present = 1u;
            s_cluster[sl].chest_gx = cd->item_spawns[si].x;
            s_cluster[sl].chest_gy = cd->item_spawns[si].y;
            placed++;
        }
    }
}

/* Convertit un bit STATIC_ROOM_EXIT_* en direction (0..3). */
static u8 cluster_dir_from_exit_mask(u8 exit_mask)
{
    if (exit_mask == STATIC_ROOM_EXIT_N) return 0u;
    if (exit_mask == STATIC_ROOM_EXIT_E) return 1u;
    if (exit_mask == STATIC_ROOM_EXIT_S) return 2u;
    return 3u;  /* W */
}

/* =========================================================================
 * MKD-5 : State Minimap - vue arbre cluster
 * =========================================================================
 * Affiche jusqu'a 5 rooms (slots du cluster) avec leurs liens (neighbors[4]).
 * Layout BFS depuis slot 0 : N=haut, E=droite, S=bas, W=gauche.
 * Marqueurs :
 *   - room normale       : metatile FLOOR_1 (sable)
 *   - room courante      : metatile PILLAR  (bloc fonce, distinct)
 *   - room avec stair    : metatile STAIR au centre
 *   - lien interne       : tile DOOR_N/DOOR_W (1 tile entre 2 rooms)
 *   - exit STAIR cluster : marqueur escalier sur la case voisine
 * Sortie : PAD_B ou PAD_OPTION -> retour au state precedent (s_minimap_return_state,
 *          a setter par le caller AVANT de passer en STATE_MINIMAP).
 * ========================================================================= */

/* s_minimap_return_state declare en tete de fichier (apres s_state) */

static s8 s_mm_gx[CLUSTER_MAX_ROOMS];
static s8 s_mm_gy[CLUSTER_MAX_ROOMS];
static u8 s_mm_placed[CLUSTER_MAX_ROOMS];

/* MKD-5 v2 : assets dedies (map_room 3x3, map_jonction 1x1, player sprite). */
#define MM_ROOM_TILE_BASE        400u
#define MM_JONCTION_H_TILE_BASE  401u   /* lien horizontal (E/W) */
#define MM_JONCTION_V_TILE_BASE  402u   /* lien vertical (N/S, source rotee 90deg) */
#define MM_STEP                  4u    /* 3 tiles room + 1 tile lien */
#define MM_ORIGIN_X            7u    /* tile col du TL room a grid (0,0) */
#define MM_ORIGIN_Y            5u    /* tile row du TL room a grid (0,0) */
#define MM_PLAYER_SPR_BASE     0u    /* slot OAM */
#define MM_PLAYER_ANIM_TICKS   18u   /* frames entre 2 anim frames */

static u8 s_mm_player_frame = 0u;
static u8 s_mm_player_timer = 0u;

static void minimap_compute_layout(void)
{
    u8 i, d, nb;
    u8 changed;

    for (i = 0u; i < CLUSTER_MAX_ROOMS; i++) {
        s_mm_gx[i] = 0;
        s_mm_gy[i] = 0;
        s_mm_placed[i] = 0u;
    }
    /* Salle de depart (STATE_ROOM) : aucun cluster genere. On place quand meme
     * la salle 0 a l'origine pour afficher "une salle seule" au lieu d'un
     * ecran noir. */
    s_mm_placed[0] = 1u;
    if (s_cluster_count == 0u) return;

    changed = 1u;
    while (changed) {
        changed = 0u;
        for (i = 0u; i < s_cluster_count; i++) {
            if (!s_mm_placed[i]) continue;
            for (d = 0u; d < 4u; d++) {
                nb = s_cluster[i].neighbors[d];
                if (nb >= CLUSTER_MAX_ROOMS) continue;
                if (s_mm_placed[nb]) continue;
                s_mm_gx[nb] = s_mm_gx[i];
                s_mm_gy[nb] = s_mm_gy[i];
                if (d == 0u)      s_mm_gy[nb]--;
                else if (d == 1u) s_mm_gx[nb]++;
                else if (d == 2u) s_mm_gy[nb]++;
                else              s_mm_gx[nb]--;
                s_mm_placed[nb] = 1u;
                changed = 1u;
            }
        }
    }
}

/* Pose une room 3x3 tiles a la tile (tx, ty) top-left, en utilisant
 * map_room (1 unique tile repete 9 fois). Palette slot 0 sur SCR1. */
static void minimap_put_room(u8 tx, u8 ty)
{
    u8 dx, dy;
    for (dy = 0u; dy < 3u; dy++) {
        for (dx = 0u; dx < 3u; dx++) {
            ngpc_gfx_put_tile_ex(GFX_SCR1, (u8)(tx + dx), (u8)(ty + dy),
                MM_ROOM_TILE_BASE, 0u, 0u, 0u);
        }
    }
}

/* axis = 0 -> horizontal (E/W), 1 -> vertical (N/S). Tiles distincts car
 * pas de rotation hw NGPC (uniquement H/V flip qui ne suffit pas pour 90deg).
 * pal_slot : 0 = jonction normale (blanc), 1 = STAIR exit (rouge). */
static void minimap_put_jonction(u8 tx, u8 ty, u8 axis, u8 pal_slot)
{
    u16 tile = (axis == 0u) ? MM_JONCTION_H_TILE_BASE : MM_JONCTION_V_TILE_BASE;
    ngpc_gfx_put_tile_ex(GFX_SCR1, tx, ty, tile, pal_slot, 0u, 0u);
}

/* Top-left tile de la room placee a grid (gx, gy). MM_STEP=4, MM_ORIGIN_X/Y
 * positionnent la grille pour centrer le cluster a l'ecran. */
static void minimap_screen_xy(s8 gx, s8 gy, u8 *out_tx, u8 *out_ty)
{
    *out_tx = (u8)((s8)MM_ORIGIN_X + (s8)(gx * (s8)MM_STEP));
    *out_ty = (u8)((s8)MM_ORIGIN_Y + (s8)(gy * (s8)MM_STEP));
}

static void minimap_draw(void)
{
    u8 i, d, nb;
    u8 tx, ty;
    u8 link_tx, link_ty;

    /* Salle de depart : pas de cluster -> une seule salle, aucun lien (les
     * neighbors de s_cluster[0] peuvent etre obsoletes d'un run precedent). */
    if (s_cluster_count == 0u) {
        minimap_screen_xy(s_mm_gx[0], s_mm_gy[0], &tx, &ty);
        minimap_put_room(tx, ty);
        return;
    }

    for (i = 0u; i < s_cluster_count; i++) {
        if (!s_mm_placed[i]) continue;
        minimap_screen_xy(s_mm_gx[i], s_mm_gy[i], &tx, &ty);

        /* Toutes les rooms du cluster -> meme tile map_room. La room
         * courante est mise en evidence par le sprite player_position
         * dessine par-dessus (cf minimap_draw_player). */
        minimap_put_room(tx, ty);

        /* Liens internes UNIQUEMENT (slot ↔ slot dans le cluster).
         * Les STAIR exits ne sont PAS affiches : les doors qui pointaient
         * vers STAIR sont traitees comme des murs en jeu (cf dungeon_try_move
         * qui ne fait plus rien dans ce cas). */
        for (d = 0u; d < 4u; d++) {
            nb = s_cluster[i].neighbors[d];
            if (nb == CLUSTER_NEIGHBOR_NONE) continue;
            if (nb == CLUSTER_NEIGHBOR_STAIR) continue;
            if (nb >= CLUSTER_MAX_ROOMS) continue;
            if (!s_mm_placed[nb]) continue;

            {
                s8 expected_gx = s_mm_gx[i];
                s8 expected_gy = s_mm_gy[i];
                u8 axis = 0u;
                if (d == 0u)      { expected_gy--; axis = 1u; }
                else if (d == 1u) { expected_gx++; axis = 0u; }
                else if (d == 2u) { expected_gy++; axis = 1u; }
                else              { expected_gx--; axis = 0u; }

                if (s_mm_gx[nb] != expected_gx) continue;
                if (s_mm_gy[nb] != expected_gy) continue;

                link_tx = (u8)(tx + 1u);
                link_ty = (u8)(ty + 1u);
                if (d == 0u)      link_ty = (u8)(ty - 1u);
                else if (d == 1u) link_tx = (u8)(tx + 3u);
                else if (d == 2u) link_ty = (u8)(ty + 3u);
                else              link_tx = (u8)(tx - 1u);
                minimap_put_jonction(link_tx, link_ty, axis, 0u);
            }
        }
    }
}

/* Pose le sprite player_position (8x8 anime) au CENTRE de la room
 * courante (cluster_current). Tile (tx+1, ty+1) = centre du 3x3. */
static void minimap_draw_player(void)
{
    u8 tx, ty;
    s16 sx, sy;
    const NgpcMetasprite *frame;
    /* Salle de depart (pas de cluster) : marqueur sur la salle 0
     * (s_cluster_current peut etre obsolete d'un run precedent). */
    u8 cur = (s_cluster_count == 0u) ? 0u : s_cluster_current;

    if (cur >= CLUSTER_MAX_ROOMS) return;
    if (!s_mm_placed[cur]) return;

    minimap_screen_xy(s_mm_gx[cur], s_mm_gy[cur], &tx, &ty);
    /* Centre 3x3 = (tx+1, ty+1) tile. Position pixel = tile * 8. */
    sx = (s16)((u8)(tx + 1u) * 8u);
    sy = (s16)((u8)(ty + 1u) * 8u);

    frame = (s_mm_player_frame == 0u) ?
        &map_player_position_frame_0 : &map_player_position_frame_1;
    ngpc_mspr_draw(MM_PLAYER_SPR_BASE, sx, sy, frame, (u8)SPR_FRONT);
}

static void minimap_init(void)
{
    u8 i;
    u16 src;

    hud_raster_disable();
    ngpc_gfx_set_viewport(0, 0, SCREEN_W, SCREEN_H);
    HW_SCR_PRIO = 0x00;  /* SCR1 front, SCR2 behind, sprites au-dessus */
    ngpc_gfx_scroll(GFX_SCR1, 0, 0);
    ngpc_gfx_scroll(GFX_SCR2, 0, 0);
    ngpc_gfx_clear(GFX_SCR1);
    ngpc_gfx_clear(GFX_SCR2);
    ngpc_sprite_hide_all();
    ngpc_gfx_set_bg_color(RGB(0, 0, 0));

    /* Load map_room tiles (1 unique tile, 8 words). */
    ngpc_gfx_load_tiles_at(map_room_tiles, map_room_tiles_count,
        MM_ROOM_TILE_BASE);
    /* Load map_jonction horizontal + vertical (2 tiles distincts). */
    ngpc_gfx_load_tiles_at(map_jonction_tiles, map_jonction_tiles_count,
        MM_JONCTION_H_TILE_BASE);
    ngpc_gfx_load_tiles_at(map_jonction_v_tiles, map_jonction_v_tiles_count,
        MM_JONCTION_V_TILE_BASE);

    /* Palette SCR1 slot 0 = map_room palette */
    ngpc_gfx_set_palette(GFX_SCR1, 0u,
        map_room_palettes[0], map_room_palettes[1],
        map_room_palettes[2], map_room_palettes[3]);
    /* Palette SCR1 slot 1 = STAIR exit indicator (rouge au lieu de blanc).
     * Meme tile que jonction, mais palette differente -> couleur differente
     * pour signaler "porte hors cluster" au joueur. */
    ngpc_gfx_set_palette(GFX_SCR1, 1u,
        map_room_palettes[0],  /* transparent (slot 0) */
        RGB(15, 4, 4),         /* rouge vif (slot 1) */
        map_room_palettes[2],  /* keep */
        map_room_palettes[3]);

    /* Sprite player_position : tiles + palette. */
    ngpc_gfx_load_tiles_at(map_player_position_tiles,
        map_player_position_tiles_count,
        map_player_position_tile_base);
    for (i = 0u; i < map_player_position_palette_count; i++) {
        src = (u16)i * 4u;
        ngpc_gfx_set_palette(GFX_SPR, (u8)(map_player_position_pal_base + i),
            map_player_position_palettes[src],
            map_player_position_palettes[(u16)(src + 1u)],
            map_player_position_palettes[(u16)(src + 2u)],
            map_player_position_palettes[(u16)(src + 3u)]);
    }

    minimap_compute_layout();
    minimap_draw();

    /* Reset anim state */
    s_mm_player_frame = 0u;
    s_mm_player_timer = 0u;
    minimap_draw_player();
}

static void minimap_update(void)
{
    if (ngpc_pad_pressed & (PAD_OPTION | PAD_B)) {
        s_state = s_minimap_return_state;
        return;
    }

    /* Anim sprite player 2-frame */
    s_mm_player_timer++;
    if (s_mm_player_timer >= MM_PLAYER_ANIM_TICKS) {
        s_mm_player_timer = 0u;
        s_mm_player_frame ^= 1u;
    }
    minimap_draw_player();
}

/* ---- Dungeon enemy logic ---------------------------------------------- */

static u8 dungeon_enemy_spr_base(u8 idx)
{
    if (idx == 0u) return DUNG_ENEMY_SPR_BASE_0;
    if (idx == 1u) return DUNG_ENEMY_SPR_BASE_1;
    return DUNG_ENEMY_SPR_BASE_2;
}

static const NgpcMetasprite *dungeon_enemy_frame(u8 type, u8 anim_frame)
{
    if (type == (u8)ENEMY_TYPE_SKULL) {
        return (anim_frame & 1u) ? &skull_frame_1 : &skull_frame_0;
    }
    if (type == (u8)ENEMY_TYPE_FLAMME) {
        return (anim_frame & 1u) ? &flamme_frame_1 : &flamme_frame_0;
    }
    if (type == (u8)ENEMY_TYPE_HENT) {
        return (anim_frame & 1u) ? &hent_frame_1 : &hent_frame_0;
    }
    /* SLIME : on utilise les frames idle 0 et 1 du sheet. */
    return (anim_frame & 1u) ? &lime_sheet_frame_1 : &lime_sheet_frame_0;
}

static u8 dungeon_enemy_draw_flags(PlayerDirection dir)
{
    /* SPR_MIDDLE : enemies entre SCR1 (room) et SCR2 (HUD/popups/decor). */
    if (dir == PLAYER_DIR_LEFT)
        return (u8)(SPR_MIDDLE | SPR_HFLIP);
    return (u8)SPR_MIDDLE;
}

static void dungeon_load_enemies_from_cluster(u8 slot)
{
    u8 i;
    u8 dst;

    s_dung_enemy_count = 0u;
    if (slot >= CLUSTER_MAX_ROOMS)
        return;

    dst = 0u;
    for (i = 0u; i < CLUSTER_MAX_ACTORS_PER_ROOM && dst < DUNG_MAX_ENEMIES; i++) {
        if (!s_cluster[slot].actors[i].alive)
            continue;

        s_dung_enemies[dst].alive = 1u;
        s_dung_enemies[dst].type = s_cluster[slot].actors[i].type_id;
        s_dung_enemies[dst].hp = s_cluster[slot].actors[i].hp;
        s_dung_enemies[dst].gx = s_cluster[slot].actors[i].gx;
        s_dung_enemies[dst].gy = s_cluster[slot].actors[i].gy;
        s_dung_enemies[dst].dir = (PlayerDirection)s_cluster[slot].actors[i].dir;
        s_dung_enemies[dst].mv_dx = s_cluster[slot].actors[i].mv_dx;
        s_dung_enemies[dst].mv_dy = s_cluster[slot].actors[i].mv_dy;
        s_dung_enemies[dst].anim_frame = 0u;
        s_dung_enemies[dst].anim_timer = (u8)(dst * 4u); /* desync */
        dst++;
    }
    s_dung_enemy_count = dst;
}

static void dungeon_save_enemies_to_cluster(u8 slot)
{
    u8 i;

    if (slot >= CLUSTER_MAX_ROOMS)
        return;

    /* Reset persistent slots, then write back. */
    for (i = 0u; i < CLUSTER_MAX_ACTORS_PER_ROOM; i++) {
        s_cluster[slot].actors[i].alive = 0u;
    }
    s_cluster[slot].actor_count = 0u;

    for (i = 0u; i < s_dung_enemy_count; i++) {
        if (!s_dung_enemies[i].alive)
            continue;
        s_cluster[slot].actors[i].alive = 1u;
        s_cluster[slot].actors[i].hp = s_dung_enemies[i].hp;
        s_cluster[slot].actors[i].type_id = s_dung_enemies[i].type;
        s_cluster[slot].actors[i].gx = s_dung_enemies[i].gx;
        s_cluster[slot].actors[i].gy = s_dung_enemies[i].gy;
        s_cluster[slot].actors[i].dir = (u8)s_dung_enemies[i].dir;
        s_cluster[slot].actors[i].mv_dx = s_dung_enemies[i].mv_dx;
        s_cluster[slot].actors[i].mv_dy = s_dung_enemies[i].mv_dy;
        s_cluster[slot].actor_count++;
    }
}

static u8 dungeon_enemy_at(s8 gx, s8 gy)
{
    u8 i;
    if (gx < 0 || gy < 0)
        return 0xFFu;
    for (i = 0u; i < s_dung_enemy_count; i++) {
        if (!s_dung_enemies[i].alive) continue;
        if (s_dung_enemies[i].gx == (u8)gx && s_dung_enemies[i].gy == (u8)gy)
            return i;
    }
    return 0xFFu;
}

/* Collision dispo pour un enemy : tient compte du type (FLAMME ignore VOID),
 * du player et des autres enemies. */
static u8 dungeon_enemy_can_enter(u8 type, s8 gx, s8 gy)
{
    u8 col;
    u8 i;

    if (gx < 0 || gy < 0)
        return 0u;
    if ((u8)gx >= static_room_w() || (u8)gy >= static_room_h())
        return 0u;

    col = static_room_collision_at(gx, gy);
    if (col == STATIC_ROOM_COL_SOLID)
        return 0u;
    if (col == STATIC_ROOM_COL_STAIR)
        return 0u;
    if (col == STATIC_ROOM_COL_VOID) {
        if (type != (u8)ENEMY_TYPE_FLAMME)
            return 0u;
    }

    if ((u8)gx == s_dung_gx && (u8)gy == s_dung_gy)
        return 0u;

    /* MKD-chest : un coffre ferme occupe la case (les enemies ne le
     * traversent pas). */
    if (dungeon_chest_at(gx, gy))
        return 0u;

    for (i = 0u; i < s_dung_enemy_count; i++) {
        if (!s_dung_enemies[i].alive) continue;
        if (s_dung_enemies[i].gx == (u8)gx && s_dung_enemies[i].gy == (u8)gy)
            return 0u;
    }
    return 1u;
}

static u8 dungeon_enemy_adjacent_player(u8 idx)
{
    u8 dx;
    u8 dy;
    DungeonEnemy *e = &s_dung_enemies[idx];

    dx = grid_abs_diff(e->gx, s_dung_gx);
    dy = grid_abs_diff(e->gy, s_dung_gy);
    return ((u8)(dx + dy) == 1u) ? 1u : 0u;
}

/* ---- Dungeon selector / attack effect / damage popup ---- */

static u8 dung_popup_tx(u8 gx)
{
    return (u8)(gx * 2u);
}

static u8 dung_popup_ty(u8 gy)
{
    return (gy > 0u) ? (u8)((u8)(gy * 2u) - 1u) : 0u;
}

static void dung_popup_erase(void)
{
    if (!s_dung_popup_visible) return;
    ngpc_text_print(GFX_SCR2, 0u,
        dung_popup_tx(s_dung_popup_gx),
        dung_popup_ty(s_dung_popup_gy),
        "  ");
}

static void dung_popup_hide(void)
{
    if (s_dung_popup_visible) {
        if (s_dung_popup_is_fatal) {
            ngpc_mspr_hide((u8)SKULL_DEATH_SPR_BASE, 4u);
        } else {
            dung_popup_erase();
        }
    }
    s_dung_popup_visible = 0u;
    s_dung_popup_value = 0u;
    s_dung_popup_timer = 0u;
    s_dung_popup_source = ATTACK_EFFECT_NONE;
    s_dung_popup_is_fatal = 0u;
}

static void dung_popup_draw(void)
{
    if (!s_dung_popup_visible) return;

    if (s_dung_popup_is_fatal) {
        /* Skull 16x16 au-dessus du joueur (au lieu du nombre). Position
         * pixel screen = pos monde - scroll camera, identique a
         * dungeon_draw_player (cf ligne 2485-2486). */
        s16 sx = (s16)(s_dung_px - s_dung_cam_x);
        s16 sy = (s16)((s16)(s_dung_py - s_dung_cam_y) - 16);
        /* SPR_FRONT : skull death popup doit toujours etre au-dessus
         * (enemies + HUD), c'est un evenement fatal rare et il faut le voir. */
        ngpc_mspr_draw((u8)SKULL_DEATH_SPR_BASE, sx, sy,
            &skull_death_frame_0, (u8)SPR_FRONT);
        return;
    }

    ngpc_text_print_num(GFX_SCR2, 0u,
        dung_popup_tx(s_dung_popup_gx),
        dung_popup_ty(s_dung_popup_gy),
        s_dung_popup_value, 2u);
}

static void dung_popup_start(u8 gx, u8 gy, u8 value, AttackEffectSource src)
{
    s_dung_popup_visible = 1u;
    s_dung_popup_gx = gx;
    s_dung_popup_gy = gy;
    s_dung_popup_value = value;
    s_dung_popup_timer = DAMAGE_POPUP_FRAMES;
    s_dung_popup_source = src;
    /* Detect hit fatal : enemy a tue le joueur (HP=0). */
    s_dung_popup_is_fatal = (u8)
        ((src == ATTACK_EFFECT_ENEMY && g_player.hp == 0u) ? 1u : 0u);
}

static void dung_sel_hide(void)
{
    s_dung_sel_visible = 0u;
    ngpc_mspr_hide(DUNG_FX_SPR_BASE, 4u);
}

static void dung_sel_show(u8 gx, u8 gy)
{
    s_dung_sel_visible = 1u;
    s_dung_sel_gx = gx;
    s_dung_sel_gy = gy;
}

static void dung_sel_draw(void)
{
    s16 sx;
    s16 sy;
    if (!s_dung_sel_visible) return;
    sx = (s16)((s16)((s16)s_dung_sel_gx * 16) - s_dung_cam_x);
    sy = (s16)((s16)((s16)s_dung_sel_gy * 16) - s_dung_cam_y);
    ngpc_mspr_draw(DUNG_FX_SPR_BASE, sx, sy, &selecteur_frame_0, (u8)SPR_MIDDLE);
}

static void dung_atk_hide(void)
{
    s_dung_atk_visible = 0u;
    s_dung_atk_visual = ATTACK_EFFECT_VISUAL_NORMAL;
    s_dung_atk_source = ATTACK_EFFECT_NONE;
    ngpc_mspr_hide(DUNG_FX_SPR_BASE, 4u);
}

static void dung_atk_draw(void)
{
    const NgpcMetasprite *frame;
    u8 flags;
    s16 sx;
    s16 sy;

    if (!s_dung_atk_visible) {
        return;
    }

    frame = (s_dung_atk_visual == ATTACK_EFFECT_VISUAL_CRIT)
        ? &effect_attaque_crit_frame_0
        : &effect_attaque_frame_0;
    /* SPR_MIDDLE pour rester sous le HUD (SCR2). */
    flags = (u8)SPR_MIDDLE;
    if (s_dung_atk_timer >= ATTACK_EFFECT_FRAME_TICKS)
        flags = (u8)(flags | SPR_HFLIP);

    sx = (s16)((s16)((s16)s_dung_atk_gx * 16) - s_dung_cam_x);
    sy = (s16)((s16)((s16)s_dung_atk_gy * 16) - s_dung_cam_y);
    ngpc_mspr_draw(DUNG_FX_SPR_BASE, sx, sy, frame, flags);
}

static void dung_atk_start(u8 gx, u8 gy, u8 visual,
    AttackEffectSource src, u8 dmg)
{
    /* Hide selector first (shared OAM slot DUNG_FX_SPR_BASE). */
    s_dung_sel_visible = 0u;
    ngpc_mspr_hide(DUNG_FX_SPR_BASE, 4u);

    s_dung_atk_visible = 1u;
    s_dung_atk_gx = gx;
    s_dung_atk_gy = gy;
    s_dung_atk_timer = 0u;
    s_dung_atk_visual = visual;
    s_dung_atk_source = src;
    dung_popup_start(gx, gy, dmg, src);
    s_dung_turn_state = DUNG_TURN_ATTACK_EFFECT;
}

static void dungeon_enemy_attack_player(u8 idx)
{
    u8 visual;
    u8 dmg;

    dmg = enemy_roll_damage_typed(s_dung_enemies[idx].type, &visual);
    if (dmg > 0u)
        player_apply_damage(dmg);

    /* Affiche toujours l'effet + popup pour rendre le tour visible
     * (popup = 0 sur miss). */
    dung_atk_start(s_dung_gx, s_dung_gy, visual, ATTACK_EFFECT_ENEMY, dmg);
}

/* SLIME : aggro greedy axis-first vers le player. Si l'axe primaire est
 * bloque, essaie le secondaire. Si bloque dans les deux, reste sur place. */
static void dungeon_enemy_turn_slime(u8 idx)
{
    DungeonEnemy *e = &s_dung_enemies[idx];
    s8 dx;
    s8 dy;
    u8 abs_dx;
    u8 abs_dy;
    s8 try_dx;
    s8 try_dy;

    dx = axis_step_toward(e->gx, s_dung_gx);
    dy = axis_step_toward(e->gy, s_dung_gy);
    abs_dx = grid_abs_diff(e->gx, s_dung_gx);
    abs_dy = grid_abs_diff(e->gy, s_dung_gy);

    /* Try the dominant axis first. */
    if (abs_dx >= abs_dy && dx != 0) {
        try_dx = dx; try_dy = 0;
        if (dungeon_enemy_can_enter(e->type, (s8)((s8)e->gx + try_dx),
            (s8)((s8)e->gy + try_dy))) {
            e->gx = (u8)((s8)e->gx + try_dx);
            e->dir = (try_dx < 0) ? PLAYER_DIR_LEFT : PLAYER_DIR_RIGHT;
            return;
        }
    }
    if (dy != 0) {
        try_dx = 0; try_dy = dy;
        if (dungeon_enemy_can_enter(e->type, (s8)((s8)e->gx + try_dx),
            (s8)((s8)e->gy + try_dy))) {
            e->gy = (u8)((s8)e->gy + try_dy);
            e->dir = (try_dy < 0) ? PLAYER_DIR_UP : PLAYER_DIR_DOWN;
            return;
        }
    }
    /* Fallback : axe non-dominant (si on n'a pas deja essaye dx). */
    if (dx != 0 && abs_dx < abs_dy) {
        try_dx = dx; try_dy = 0;
        if (dungeon_enemy_can_enter(e->type, (s8)((s8)e->gx + try_dx),
            (s8)((s8)e->gy + try_dy))) {
            e->gx = (u8)((s8)e->gx + try_dx);
            e->dir = (try_dx < 0) ? PLAYER_DIR_LEFT : PLAYER_DIR_RIGHT;
            return;
        }
    }
    /* Bloque : reste en place. */
}

/* SKULL : un pas par tour le long de mv_dx/mv_dy. Si le pas devant est
 * bloque (mur, void, autre actor) -> demi-tour, et si demi-tour aussi
 * bloque -> tourne 90deg en piochant un autre axe. */
static void dungeon_enemy_turn_skull(u8 idx)
{
    DungeonEnemy *e = &s_dung_enemies[idx];
    s8 nx;
    s8 ny;
    s8 cand_dx[4];
    s8 cand_dy[4];
    u8 i;

    nx = (s8)((s8)e->gx + e->mv_dx);
    ny = (s8)((s8)e->gy + e->mv_dy);

    if (dungeon_enemy_can_enter(e->type, nx, ny)) {
        e->gx = (u8)nx;
        e->gy = (u8)ny;
        if (e->mv_dx < 0) e->dir = PLAYER_DIR_LEFT;
        else if (e->mv_dx > 0) e->dir = PLAYER_DIR_RIGHT;
        else if (e->mv_dy < 0) e->dir = PLAYER_DIR_UP;
        else e->dir = PLAYER_DIR_DOWN;
        return;
    }

    /* Bloque : essayer demi-tour, puis perp. Ordre : demi-tour, perp1, perp2. */
    cand_dx[0] = (s8)(-e->mv_dx);
    cand_dy[0] = (s8)(-e->mv_dy);
    /* Si l'axe actuel est H, perp = V ; sinon perp = H. */
    if (e->mv_dx != 0) {
        cand_dx[1] = 0; cand_dy[1] = 1;
        cand_dx[2] = 0; cand_dy[2] = -1;
    } else {
        cand_dx[1] = 1; cand_dy[1] = 0;
        cand_dx[2] = -1; cand_dy[2] = 0;
    }
    /* 4eme cand : repos (no-op). */
    cand_dx[3] = 0; cand_dy[3] = 0;

    for (i = 0u; i < 4u; i++) {
        if (cand_dx[i] == 0 && cand_dy[i] == 0) {
            /* Bloque partout : on garde l'orientation. */
            return;
        }
        nx = (s8)((s8)e->gx + cand_dx[i]);
        ny = (s8)((s8)e->gy + cand_dy[i]);
        if (dungeon_enemy_can_enter(e->type, nx, ny)) {
            e->mv_dx = cand_dx[i];
            e->mv_dy = cand_dy[i];
            e->gx = (u8)nx;
            e->gy = (u8)ny;
            if (e->mv_dx < 0) e->dir = PLAYER_DIR_LEFT;
            else if (e->mv_dx > 0) e->dir = PLAYER_DIR_RIGHT;
            else if (e->mv_dy < 0) e->dir = PLAYER_DIR_UP;
            else e->dir = PLAYER_DIR_DOWN;
            return;
        }
    }
}

/* FLAMME : direction tiree au hasard chaque tour. Peut traverser les void
 * (dungeon_enemy_can_enter laisse passer si type == FLAMME). Si la
 * direction tiree est bloquee, on essaie les autres jusqu'a en trouver
 * une libre, ou on reste sur place. */
static void dungeon_enemy_turn_flamme(u8 idx)
{
    DungeonEnemy *e = &s_dung_enemies[idx];
    s8 dx_table[4] = { 1, -1, 0, 0 };
    s8 dy_table[4] = { 0, 0, 1, -1 };
    u8 start;
    u8 i;
    u8 d;
    s8 nx;
    s8 ny;

    start = (u8)(ngpc_qrandom() & 0x03u);
    for (i = 0u; i < 4u; i++) {
        d = (u8)((start + i) & 0x03u);
        nx = (s8)((s8)e->gx + dx_table[d]);
        ny = (s8)((s8)e->gy + dy_table[d]);
        if (dungeon_enemy_can_enter(e->type, nx, ny)) {
            e->gx = (u8)nx;
            e->gy = (u8)ny;
            if (dx_table[d] < 0) e->dir = PLAYER_DIR_LEFT;
            else if (dx_table[d] > 0) e->dir = PLAYER_DIR_RIGHT;
            else if (dy_table[d] < 0) e->dir = PLAYER_DIR_UP;
            else e->dir = PLAYER_DIR_DOWN;
            return;
        }
    }
}

/* HENT : passif tant que le player n'est pas aligne H ou V avec lui. Au
 * trigger, fixe une direction de charge (mv_dx/mv_dy) et avance d'une
 * case par tour DANS CETTE DIRECTION, sans la corriger si le player
 * sort ensuite de l'alignement. Continue jusqu'a :
 *   - rencontrer un obstacle (mur/void/autre actor) -> reset mv et
 *     redevient passif au tour suivant
 *   - rencontrer le player (l'attaque adjacence est geree AVANT le
 *     turn par dungeon_enemy_adjacent_player dans dungeon_run_enemy_turns)
 * Reutilise mv_dx/mv_dy de DungeonEnemy (deja a 0 au spawn pour HENT). */
static void dungeon_enemy_turn_hent(u8 idx)
{
    DungeonEnemy *e = &s_dung_enemies[idx];
    s8 step;
    s8 nx;
    s8 ny;

    /* Charge en cours : on continue droit tant que c'est libre. */
    if (e->mv_dx != 0 || e->mv_dy != 0) {
        nx = (s8)((s8)e->gx + e->mv_dx);
        ny = (s8)((s8)e->gy + e->mv_dy);
        if (dungeon_enemy_can_enter(e->type, nx, ny)) {
            e->gx = (u8)nx;
            e->gy = (u8)ny;
            if (e->mv_dx < 0) e->dir = PLAYER_DIR_LEFT;
            else if (e->mv_dx > 0) e->dir = PLAYER_DIR_RIGHT;
            else if (e->mv_dy < 0) e->dir = PLAYER_DIR_UP;
            else e->dir = PLAYER_DIR_DOWN;
        } else {
            /* Bloque -> stop, retour passif (re-trigger possible si le
             * player se realigne au tour suivant). */
            e->mv_dx = 0;
            e->mv_dy = 0;
        }
        return;
    }

    /* Passif : detecte un alignement et amorce la charge + 1er pas. */
    if (e->gx == s_dung_gx) {
        step = axis_step_toward(e->gy, s_dung_gy);
        if (step == 0)
            return;
        nx = (s8)e->gx;
        ny = (s8)((s8)e->gy + step);
        if (dungeon_enemy_can_enter(e->type, nx, ny)) {
            e->mv_dx = 0;
            e->mv_dy = step;
            e->gy = (u8)ny;
            e->dir = (step < 0) ? PLAYER_DIR_UP : PLAYER_DIR_DOWN;
        }
        /* Si bloque des le premier pas : on reste passif (mv_dx/dy a 0)
         * et le hent re-essaiera au prochain tour si player toujours
         * aligne -- ce qui mime le comportement attendu. */
        return;
    }

    if (e->gy == s_dung_gy) {
        step = axis_step_toward(e->gx, s_dung_gx);
        if (step == 0)
            return;
        nx = (s8)((s8)e->gx + step);
        ny = (s8)e->gy;
        if (dungeon_enemy_can_enter(e->type, nx, ny)) {
            e->mv_dx = step;
            e->mv_dy = 0;
            e->gx = (u8)nx;
            e->dir = (step < 0) ? PLAYER_DIR_LEFT : PLAYER_DIR_RIGHT;
        }
        return;
    }
    /* Pas aligne : reste immobile. */
}

/* Iteration des tours enemies. Re-entree possible : si un enemy attaque
 * le player, on lance l'effet d'attaque et on rend la main (l'iter
 * reprend apres la fin du popup). Sortie quand iter >= count -> retour
 * a WAIT_INPUT. */
static void dungeon_run_enemy_turns(void)
{
    while (s_dung_turn_iter < s_dung_enemy_count) {
        u8 i = s_dung_turn_iter;
        s_dung_turn_iter++;

        if (!s_dung_enemies[i].alive)
            continue;

        if (dungeon_enemy_adjacent_player(i)) {
            /* Face le player avant l'attaque. */
            if (s_dung_enemies[i].gx > s_dung_gx)
                s_dung_enemies[i].dir = PLAYER_DIR_LEFT;
            else if (s_dung_enemies[i].gx < s_dung_gx)
                s_dung_enemies[i].dir = PLAYER_DIR_RIGHT;
            else if (s_dung_enemies[i].gy > s_dung_gy)
                s_dung_enemies[i].dir = PLAYER_DIR_UP;
            else
                s_dung_enemies[i].dir = PLAYER_DIR_DOWN;

            dungeon_enemy_attack_player(i);
            return;  /* l'effet/popup terminera et nous rappellera */
        }

        switch (s_dung_enemies[i].type) {
        case (u8)ENEMY_TYPE_SLIME:  dungeon_enemy_turn_slime(i);  break;
        case (u8)ENEMY_TYPE_SKULL:  dungeon_enemy_turn_skull(i);  break;
        case (u8)ENEMY_TYPE_FLAMME: dungeon_enemy_turn_flamme(i); break;
        case (u8)ENEMY_TYPE_HENT:   dungeon_enemy_turn_hent(i);   break;
        default: break;
        }
    }

    /* Tous les enemies ont joue leur tour : retour input + persiste. */
    s_dung_turn_state = DUNG_TURN_WAIT_INPUT;
    dungeon_save_enemies_to_cluster(s_cluster_current);
}

static void dung_begin_enemy_turns(void)
{
    s_dung_turn_iter = 0u;
    s_dung_turn_state = DUNG_TURN_ENEMY_TURNS;
    /* MKD-depth : 1 tour = 1 action player + tour enemies. On compte ici
     * (declenche apres chaque action player qui consomme un tour : bump,
     * move, attaque-confirm, wait). g_run_turns affiche au victory screen. */
    if (g_run_turns < 0xFFFFu) g_run_turns++;
}

/* Apres l'effet+popup d'une attaque, repartir vers le bon etat selon
 * l'origine. */
static void dung_finish_attack_resolution(AttackEffectSource src)
{
    if (g_player.hp == 0u) {
        /* Mort du player : freeze sur wait input. */
        s_dung_turn_state = DUNG_TURN_WAIT_INPUT;
        return;
    }

    if (src == ATTACK_EFFECT_PLAYER) {
        /* Cleanup enemies morts visibles + sauvegarde, puis tour enemies. */
        dung_begin_enemy_turns();
    } else if (src == ATTACK_EFFECT_ENEMY) {
        /* Continue iteration des autres enemies (s_dung_turn_iter
         * pointe deja sur le suivant). */
        s_dung_turn_state = DUNG_TURN_ENEMY_TURNS;
    } else {
        s_dung_turn_state = DUNG_TURN_WAIT_INPUT;
    }
}

static void dungeon_player_attack_enemy(u8 idx)
{
    u8 visual;
    u8 dmg;
    u8 target_gx;
    u8 target_gy;

    target_gx = s_dung_enemies[idx].gx;
    target_gy = s_dung_enemies[idx].gy;

    dmg = combat_roll_damage(g_player.atk_base, &visual);
    if (dmg > 0u) {
        if (dmg >= s_dung_enemies[idx].hp) {
            s_dung_enemies[idx].hp = 0u;
            s_dung_enemies[idx].alive = 0u;
            /* MKD-depth : compteur kills affiche au victory screen. */
            if (g_run_kills < 255u) g_run_kills++;
        } else {
            s_dung_enemies[idx].hp = (u8)(s_dung_enemies[idx].hp - dmg);
        }
    }

    /* Toujours afficher l'effet + popup (miss = popup 0). */
    dung_atk_start(target_gx, target_gy, visual, ATTACK_EFFECT_PLAYER, dmg);
}

static void dungeon_hide_enemy_sprites(void)
{
    ngpc_mspr_hide(DUNG_ENEMY_SPR_BASE_0, 4u);
    ngpc_mspr_hide(DUNG_ENEMY_SPR_BASE_1, 4u);
    ngpc_mspr_hide(DUNG_ENEMY_SPR_BASE_2, 4u);
}

static void dungeon_draw_enemies(void)
{
    u8 i;
    s16 sx;
    s16 sy;
    u8 spr;
    DungeonEnemy *e;

    for (i = 0u; i < DUNG_MAX_ENEMIES; i++) {
        spr = dungeon_enemy_spr_base(i);
        if (i >= s_dung_enemy_count) {
            ngpc_mspr_hide(spr, 4u);
            continue;
        }
        e = &s_dung_enemies[i];
        if (!e->alive) {
            ngpc_mspr_hide(spr, 4u);
            continue;
        }
        sx = (s16)((s16)((s16)e->gx * 16) - s_dung_cam_x);
        sy = (s16)((s16)((s16)e->gy * 16) - s_dung_cam_y);
        ngpc_mspr_draw(spr, sx, sy,
            dungeon_enemy_frame(e->type, e->anim_frame),
            dungeon_enemy_draw_flags(e->dir));
    }
}

static void dungeon_animate_enemies(void)
{
    u8 i;
    for (i = 0u; i < s_dung_enemy_count; i++) {
        s_dung_enemies[i].anim_timer++;
        if (s_dung_enemies[i].anim_timer >= 24u) {
            s_dung_enemies[i].anim_timer = 0u;
            s_dung_enemies[i].anim_frame ^= 1u;
        }
    }
}

/* MKD-depth : descente vers le cluster suivant (stair PAD_A ou void).
 * Si depth_current >= depth_target -> STATE_VICTORY (fin de donjon). Sinon
 * incremente depth_current + regenere un cluster + spawn aleatoire. */
static void dungeon_descend(void)
{
    dung_sel_hide();
    dungeon_save_enemies_to_cluster(s_cluster_current);
    if (g_depth_current >= g_depth_target) {
        s_state = STATE_VICTORY;
        return;
    }
    g_depth_current++;
    cluster_generate();
    s_cluster_current = (s_cluster_count > 0u) ?
        (u8)(cluster_rng_u8() % s_cluster_count) : 0u;
    dungeon_enter_room(s_cluster[s_cluster_current].bank_idx,
        STATIC_ROOM_ENTRY_NONE);
    s_dung_turn_state = DUNG_TURN_WAIT_INPUT;
}

/* Tentative de deplacement joueur d'une case. Modele salle_01 :
 *   - target = enemy  -> selector ON, on n'avance PAS, on ne consomme PAS
 *   - target = mur    -> bump, consomme le tour
 *   - target = libre  -> move, consomme le tour
 * Le tour des enemies est declenche apres bump/move. Selector + PAD_A
 * declenche l'attaque (player_attack_enemy via dungeon_confirm_selector).
 */
/* MKD-chest : 1 si la salle courante a un coffre non ouvert a (gx,gy). */
static u8 dungeon_chest_at(s8 gx, s8 gy)
{
    ClusterRoom *cr;
    if (s_cluster_current >= CLUSTER_MAX_ROOMS) return 0u;
    cr = &s_cluster[s_cluster_current];
    if (!cr->chest_present) return 0u;
    if ((s8)cr->chest_gx == gx && (s8)cr->chest_gy == gy) return 1u;
    return 0u;
}

/* MKD-chest : ouvre le coffre de la salle courante -> item aleatoire dans
 * l'inventaire (taux de drop a definir plus tard ; provisoire 50/50
 * potion/antidote), coffre retire (disparait + reste ouvert). Retourne
 * l'item_id recu, ou 0 si l'inventaire etait plein (drop perdu). NE consomme
 * PAS le tour ici : c'est le message de loot qui enchaine les tours. */
static u8 dungeon_open_chest(void)
{
    u8 item;
    ClusterRoom *cr;
    if (s_cluster_current >= CLUSTER_MAX_ROOMS) return 0u;
    cr = &s_cluster[s_cluster_current];
    if (!cr->chest_present) return 0u;

    item = (ngpc_qrandom() & 1u) ? (u8)ITEM_ID_POTION : (u8)ITEM_ID_ANTIDOTE;
    if (player_state_inv_add(item, 1u) != 0u) {
        item = 0u;  /* inventaire plein -> drop perdu */
    }

    cr->chest_present = 0u;
    ngpc_mspr_hide((u8)DUNG_CHEST_SPR_BASE, 4u);
    return item;
}

/* MKD-chest : ecrit le message de loot par-dessus le HUD (2 rangees SCR2
 * 30-31, font opaque = barre noire + texte blanc). 20 cols de large. */
static void dungeon_chest_msg_draw(void)
{
    hud_text_print(GFX_SCR2, 0u, (u8)HUD_BG_TILE_Y,          "                    ");
    if (s_dung_chest_msg_item == (u8)ITEM_ID_POTION) {
        hud_text_print(GFX_SCR2, 0u, (u8)(HUD_BG_TILE_Y + 1u), " RECU: POTION       ");
    } else if (s_dung_chest_msg_item == (u8)ITEM_ID_ANTIDOTE) {
        hud_text_print(GFX_SCR2, 0u, (u8)(HUD_BG_TILE_Y + 1u), " RECU: ANTIDOTE     ");
    } else {
        hud_text_print(GFX_SCR2, 0u, (u8)(HUD_BG_TILE_Y + 1u), " SAC PLEIN !        ");
    }
}

/* Demarre l'affichage du message de loot : cache les enemies (ambiance
 * "event"), pose le message, passe en etat DUNG_TURN_CHEST_MSG. */
static void dungeon_chest_msg_start(u8 item)
{
    s_dung_chest_msg_item = item;
    s_dung_chest_msg_timer = (u8)DUNG_CHEST_MSG_TICKS;
    dungeon_hide_enemy_sprites();
    dungeon_chest_msg_draw();
    s_dung_turn_state = DUNG_TURN_CHEST_MSG;
}

/* Tick du message : timer ou A/B le ferme -> restaure le HUD, redessine les
 * enemies (via le flow normal) et enchaine le tour enemies (ouvrir = 1 tour). */
static void dungeon_chest_msg_update(void)
{
    if (s_dung_chest_msg_timer > 0u) s_dung_chest_msg_timer--;
    /* Dismiss A/B, mais pas dans les 1eres frames (anti-repeat du A
     * qui a ouvert le coffre). */
    if (s_dung_chest_msg_timer < (u8)(DUNG_CHEST_MSG_TICKS - 8u)) {
        if (ngpc_pad_pressed & (PAD_A | PAD_B)) s_dung_chest_msg_timer = 0u;
    }
    if (s_dung_chest_msg_timer == 0u) {
        hud_paint_bg();   /* restaure la barre HUD + "PV:" */
        hud_draw();       /* re-ecrit le chiffre HP + profondeur */
        dung_begin_enemy_turns();
    }
}

static void dungeon_try_move(s8 dx, s8 dy, PlayerDirection dir)
{
    s8 nx;
    s8 ny;
    u8 exit_mask;
    u8 col;
    u8 enemy_idx;

    s_player_dir = dir;
    nx = (s8)((s8)s_dung_gx + dx);
    ny = (s8)((s8)s_dung_gy + dy);

    /* MKD-lock : porte verrouillee pas fully open -> bump, consomme tour.
     * IMPORTANT : ce check doit etre AVANT le check exit_mask (qui
     * declenche la transition cluster sans collision), sinon la porte
     * fermee se comporte comme une porte ouverte. */
    if (static_room_lock_blocks_at((u8)nx, (u8)ny)) {
        dung_sel_hide();
        dung_begin_enemy_turns();
        return;
    }

    /* Sortie par une porte : transition cluster UNIQUEMENT vers un neighbor
     * slot valide. MKD-5 v3 : si neighbors[d] == STAIR/NONE, la porte est
     * scellee (static_room_seal_doors) -> collision SOLID -> on n'arrive
     * jamais ici pour un nb invalide. La seule facon de changer de cluster
     * est stair_socket (col == STATIC_ROOM_COL_STAIR) ou void. */
    exit_mask = dungeon_exit_at(nx, ny);
    if (exit_mask != 0u) {
        u8 d = cluster_dir_from_exit_mask(exit_mask);
        u8 nb = s_cluster[s_cluster_current].neighbors[d];
        u8 entry = CLUSTER_DIR_TO_ENTRY[d];

        if (nb < CLUSTER_MAX_ROOMS) {
            dung_sel_hide();
            dungeon_save_enemies_to_cluster(s_cluster_current);
            /* MKD-lock : sauve frame/held de la porte de l'ancienne salle
             * avant de switcher (l'etat est ensuite restaure quand on
             * revient via dungeon_apply_lock_for_current_room). */
            dungeon_save_lock_to_cluster(s_cluster_current);
            s_cluster_current = nb;
            dungeon_enter_room(s_cluster[nb].bank_idx, entry);
            s_dung_turn_state = DUNG_TURN_WAIT_INPUT;
            return;
        }
        /* Porte vers STAIR/NONE : impossible normalement (door scellee + collision
         * SOLID). Defensive : ne fait rien, le joueur reste en place. */
        return;
    }

    col = static_room_collision_at(nx, ny);
    /* STAIR : pas de transition auto. Le joueur marche dessus comme une
     * case normale, et appuie sur PAD_A pour descendre (cf
     * dungeon_update_input). Le check `col != PASS` plus bas est etendu
     * pour traiter STAIR comme PASS. */
    if (col == STATIC_ROOM_COL_VOID) {
        dungeon_descend();
        return;
    }

    /* Cible = enemy ? Affiche selector, NE consomme PAS le tour. */
    enemy_idx = dungeon_enemy_at(nx, ny);
    if (enemy_idx != 0xFFu) {
        dung_sel_show((u8)nx, (u8)ny);
        return;
    }

    /* MKD-chest : coffre cible -> selector (PAD_A ouvre), ne consomme pas le
     * tour. Le coffre bloque la case tant qu'il n'est pas ouvert. */
    if (dungeon_chest_at(nx, ny)) {
        dung_sel_show((u8)nx, (u8)ny);
        return;
    }

    dung_sel_hide();

    if (col != STATIC_ROOM_COL_PASS && col != STATIC_ROOM_COL_STAIR) {
        /* STRAT-1 : avant de traiter comme mur, check si c'est un pushable.
         * Push reussi -> avance + tour. Push bloque -> reste + tour
         * (coherence avec bump mur : tenter consomme un tour). */
        u8 pidx = static_room_pushable_at((u8)nx, (u8)ny);
        if (pidx != 0xFFu) {
            s8 bx = (s8)((s8)nx + dx);
            s8 by = (s8)((s8)ny + dy);

            /* Enemy derriere le pushable -> blocage. */
            if (dungeon_enemy_at(bx, by) != 0xFFu) {
                dung_begin_enemy_turns();
                return;
            }

            if (static_room_pushable_push((u8)nx, (u8)ny, dx, dy) == 1u) {
                /* Push reussi : le pushable a avance, joueur prend sa place. */
                s_dung_gx = (u8)nx;
                s_dung_gy = (u8)ny;
                s_dung_px = (s16)((s16)nx * 16);
                s_dung_py = (s16)((s16)ny * 16);
                dung_begin_enemy_turns();
                return;
            }
            /* Push bloque : joueur reste, tour consomme. */
            dung_begin_enemy_turns();
            return;
        }

        /* Mur normal : bump, consomme le tour. */
        dung_begin_enemy_turns();
        return;
    }

    /* Cellule libre : avance. */
    s_dung_gx = (u8)nx;
    s_dung_gy = (u8)ny;
    s_dung_px = (s16)((s16)nx * 16);
    s_dung_py = (s16)((s16)ny * 16);
    dung_begin_enemy_turns();
}

/* PAD_A confirme la cible designee par le selector. Si la cellule
 * contient un enemy vivant : attaque (= effet + popup, consomme le tour
 * via la state machine). Sinon : hide selector. */
static void dungeon_confirm_selector(void)
{
    u8 idx;
    u8 gx;
    u8 gy;

    if (!s_dung_sel_visible)
        return;

    gx = s_dung_sel_gx;
    gy = s_dung_sel_gy;
    idx = dungeon_enemy_at((s8)gx, (s8)gy);
    if (idx == 0xFFu) {
        /* Pas d'enemy : coffre ? Ouvre -> message de loot, qui enchaine
         * ensuite le tour enemies (ouvrir = 1 action). */
        if (dungeon_chest_at((s8)gx, (s8)gy)) {
            u8 got = dungeon_open_chest();
            dung_sel_hide();
            dungeon_chest_msg_start(got);
            return;
        }
        dung_sel_hide();
        return;
    }
    dungeon_player_attack_enemy(idx);
}

static void dungeon_player_wait(void)
{
    /* PAD_B sans selector : tour consomme = enemies bougent. */
    dung_sel_hide();
    dung_begin_enemy_turns();
}

static void dung_update_wait_input(void)
{
    u8 pad;

    if (g_player.hp == 0u)
        return;

    pad = (u8)(ngpc_pad_pressed | ngpc_pad_repeat);

    if (pad & PAD_UP) {
        dungeon_try_move(0, -1, PLAYER_DIR_UP);
    } else if (pad & PAD_DOWN) {
        dungeon_try_move(0, 1, PLAYER_DIR_DOWN);
    } else if (pad & PAD_LEFT) {
        dungeon_try_move(-1, 0, PLAYER_DIR_LEFT);
    } else if (pad & PAD_RIGHT) {
        dungeon_try_move(1, 0, PLAYER_DIR_RIGHT);
    } else if (ngpc_pad_pressed & PAD_A) {
        /* Selector visible -> A confirme attaque. Sinon, si joueur sur
         * une stair tile, A descend (genere nouveau cluster). */
        if (s_dung_sel_visible) {
            dungeon_confirm_selector();
        } else if (static_room_collision_at(
                       (s8)s_dung_gx, (s8)s_dung_gy) == STATIC_ROOM_COL_STAIR) {
            dungeon_descend();
        }
    } else if (ngpc_pad_pressed & PAD_B) {
        if (s_dung_sel_visible) {
            dung_sel_hide();
        } else {
            dungeon_player_wait();
        }
    }
}

static void dung_update_attack_effect(void)
{
    AttackEffectSource src;
    u8 duration;

    if (!s_dung_atk_visible) {
        src = s_dung_atk_source;
        if (s_dung_popup_visible) {
            s_dung_turn_state = DUNG_TURN_DAMAGE_POPUP;
        } else {
            dung_finish_attack_resolution(src);
        }
        return;
    }

    s_dung_atk_timer++;

    if (s_dung_popup_visible && s_dung_popup_timer > 0u)
        s_dung_popup_timer--;

    duration = (u8)(ATTACK_EFFECT_FRAME_TICKS * 2u);
    if (s_dung_atk_timer >= duration) {
        src = s_dung_atk_source;
        dung_atk_hide();
        if (s_dung_popup_visible) {
            s_dung_turn_state = DUNG_TURN_DAMAGE_POPUP;
        } else {
            dung_finish_attack_resolution(src);
        }
    }
}

static void dung_update_damage_popup(void)
{
    AttackEffectSource src;

    if (!s_dung_popup_visible) {
        src = s_dung_popup_source;
        dung_finish_attack_resolution(src);
        return;
    }
    if (s_dung_popup_timer > 0u)
        s_dung_popup_timer--;
    if (s_dung_popup_timer == 0u) {
        src = s_dung_popup_source;
        dung_popup_hide();
        dung_finish_attack_resolution(src);
    }
}

/* MKD-lock : recalcule si quelque chose tient le trigger (joueur OU enemy
 * vivant OU pushable). Appele chaque frame depuis dungeon_update (cheap :
 * exits early si pas de lock dans la salle). */
static void dungeon_refresh_lock_trigger(void)
{
    u8 tx, ty, held, i;
    if (static_room_lock_dir() == 0u) return;
    tx = static_room_lock_trigger_x();
    ty = static_room_lock_trigger_y();
    held = 0u;
    if (s_dung_gx == tx && s_dung_gy == ty) {
        held = 1u;
    } else {
        for (i = 0u; i < s_dung_enemy_count; i++) {
            if (s_dung_enemies[i].alive == 0u) continue;
            if (s_dung_enemies[i].gx == tx && s_dung_enemies[i].gy == ty) {
                held = 1u;
                break;
            }
        }
        if (held == 0u && static_room_pushable_at(tx, ty) != 0xFFu) {
            held = 1u;
        }
    }
    static_room_lock_set_held(held);
}

static void dungeon_update(void)
{
    /* Sequence de mort : freeze state machine + input, draw seulement.
     * update_death_sequence() gere le timer/fade/transition. */
    if (s_death_phase != 0u) {
        dungeon_update_camera();
        dungeon_draw_enemies();
        dungeon_draw_player();
        dungeon_draw_chest();
        dung_sel_draw();
        dung_atk_draw();
        dung_popup_draw();
        hud_draw();
        return;
    }

    /* PAD_OPTION -> pause menu (reutilise STATE_PAUSE). */
    if (ngpc_pad_pressed & PAD_OPTION) {
        s_pause_return_state = STATE_DUNGEON;
        s_state = STATE_PAUSE;
        return;
    }

    switch (s_dung_turn_state) {
    case DUNG_TURN_WAIT_INPUT:
        dung_update_wait_input();
        break;
    case DUNG_TURN_ATTACK_EFFECT:
        dung_update_attack_effect();
        break;
    case DUNG_TURN_DAMAGE_POPUP:
        dung_update_damage_popup();
        break;
    case DUNG_TURN_ENEMY_TURNS:
        dungeon_run_enemy_turns();
        break;
    case DUNG_TURN_CHEST_MSG:
        dungeon_chest_msg_update();
        break;
    }

    dungeon_animate_enemies();
    /* MKD-lock : check trigger occupant + avance anim porte si lock actif. */
    dungeon_refresh_lock_trigger();
    static_room_lock_tick();
    dungeon_update_camera();
    /* MKD-chest : pendant le message de loot, on cache les enemies et on NE
     * rafraichit PAS le HUD (sinon "PV:" ecraserait le message dans le strip).
     * Restaures a la fermeture du message (cf dungeon_chest_msg_update). */
    if (s_dung_turn_state == DUNG_TURN_CHEST_MSG) {
        dungeon_hide_enemy_sprites();
        dungeon_draw_player();
        dungeon_draw_chest();
        return;
    }
    dungeon_draw_enemies();
    dungeon_draw_player();
    dungeon_draw_chest();
    /* Selector et attack effect partagent DUNG_FX_SPR_BASE : un seul
     * des deux est visible a la fois (hide est appele sur transition). */
    dung_sel_draw();
    dung_atk_draw();
    dung_popup_draw();
    hud_draw();
}


/* ---- Death sequence + Game Over screen ---- */

/* Lance le fade-to-black sur les palettes BG visibles (SCR1 0/1 = murs+sols
 * ou tilemap room, SCR2 0/1 = font+decos). MKD-lock : ajoute SCR1 5 (porte
 * verrouillee) et 6 (declencheur) si le lock est actif dans la salle —
 * sinon les tiles restent a plein couleur jusqu'au game over. PALFX_MAX_SLOTS
 * bump a 8 pour accommoder. Les sprites sont hide separement
 * (cf update_death_sequence). */
static void death_start_fade(void)
{
    ngpc_palfx_fade_to_black(GFX_SCR1, 0u, (u8)DEATH_FADE_SPEED);
    ngpc_palfx_fade_to_black(GFX_SCR1, 1u, (u8)DEATH_FADE_SPEED);
    ngpc_palfx_fade_to_black(GFX_SCR2, 0u, (u8)DEATH_FADE_SPEED);
    ngpc_palfx_fade_to_black(GFX_SCR2, 1u, (u8)DEATH_FADE_SPEED);
    /* MKD-lock : on appelle systematiquement le fade pour 5/6 — si le
     * lock n'est pas actif, la palette est full transparent/noir donc
     * fade vers noir = no-op visuel. */
    ngpc_palfx_fade_to_black(GFX_SCR1, 5u, (u8)DEATH_FADE_SPEED);
    ngpc_palfx_fade_to_black(GFX_SCR1, 6u, (u8)DEATH_FADE_SPEED);
}

/* Appelee chaque frame dans la main loop, AVANT le switch state, pour
 * faire avancer la sequence de mort en parallele de l'update gameplay.
 * room_update/dungeon_update ignorent leur logique pendant ce temps via
 * le check s_death_phase. */
static void update_death_sequence(void)
{
    if (s_death_phase == 0u) return;

    if (s_death_phase == 1u) {
        /* Phase 1 : attente 2 sec en regardant la scene figee. */
        if (s_death_timer > 0u)
            s_death_timer--;
        if (s_death_timer == 0u) {
            ngpc_sprite_hide_all();   /* sprites disparaissent en meme temps que le fade */
            death_start_fade();
            s_death_phase = 2u;
        }
        return;
    }

    /* Phase 2 : fade en cours. */
    ngpc_palfx_update();
    /* Slot 0 = SCR1 pal 0 (le 1er lance). Quand il est fini, tous les
     * autres slots (lances le meme frame avec la meme speed) le sont
     * aussi. */
    if (!ngpc_palfx_active(0u)) {
        ngpc_palfx_stop_all();
        s_death_phase = 0u;
        s_state = STATE_GAME_OVER;
    }
}

/* "Press A" : 7 chars en sprites, row 16 (3e en partant du bas = 18-2).
 * Centre : (160 - 7*8) / 2 = 52 px. OAM 30..36 (HP text 24..29 hide pendant
 * game over donc pas de conflit, mais on prend un range disjoint pour
 * la clarte). Palette = FONT_SPR_PAL (deja installee par hud_load_vram). */
#define GAME_OVER_TEXT_DELAY  120u  /* 2 sec @ 60fps avant 1er affichage */
#define GAME_OVER_BLINK_HALF  45u   /* on 0.75s, off 0.75s -> "doucement" */
#define PRESS_A_SPR_BASE      30u
#define PRESS_A_X             52
#define PRESS_A_Y             128   /* row 16 * 8 px */

static u8 s_game_over_delay_timer = 0u;
static u8 s_game_over_blink_timer = 0u;
static u8 s_game_over_text_visible = 0u;

static void game_over_draw_press_a(void)
{
    u8 base = (u8)PRESS_A_SPR_BASE;
    u8 x = (u8)PRESS_A_X;
    u8 y = (u8)PRESS_A_Y;
    u8 pal = (u8)FONT_SPR_PAL;
    u8 flags = (u8)SPR_FRONT;

    ngpc_sprite_set((u8)(base + 0u), (u8)(x +  0), y, (u16)'P', pal, flags);
    ngpc_sprite_set((u8)(base + 1u), (u8)(x +  8), y, (u16)'r', pal, flags);
    ngpc_sprite_set((u8)(base + 2u), (u8)(x + 16), y, (u16)'e', pal, flags);
    ngpc_sprite_set((u8)(base + 3u), (u8)(x + 24), y, (u16)'s', pal, flags);
    ngpc_sprite_set((u8)(base + 4u), (u8)(x + 32), y, (u16)'s', pal, flags);
    ngpc_sprite_set((u8)(base + 5u), (u8)(x + 40), y, (u16)' ', pal, flags);
    ngpc_sprite_set((u8)(base + 6u), (u8)(x + 48), y, (u16)'A', pal, flags);
}

static void game_over_hide_press_a(void)
{
    u8 i;
    for (i = 0u; i < 7u; i++) {
        ngpc_sprite_hide((u8)(PRESS_A_SPR_BASE + i));
    }
}

static void game_over_init(void)
{
    /* State a part entiere : on RESET le contexte video comme si on
     * sortait d'un boot, sans dependre des palettes/scroll/sprites
     * laisses par l'etat precedent (dungeon, room ou fade). */

    /* 1. Desactive le raster scroll SCR2 (sinon ISR continue d'ecrire
     *    HW_SCR2_OFS_Y = HUD_BG_SCROLL_Y aux scanlines 136..151, ce
     *    qui rend visible un tile_y=30 du SCR2 -- potentiellement
     *    opaque s'il y reste les tiles du HUD precedent -- masquant
     *    Press A en sprite). */
    hud_raster_disable();

    /* 2. Reset viewport, priorite (SCR1 devant), scroll, contenu BG. */
    ngpc_gfx_set_viewport(0, 0, SCREEN_W, SCREEN_H);
    HW_SCR_PRIO = 0x00;
    ngpc_gfx_scroll(GFX_SCR1, 0, 0);
    ngpc_gfx_scroll(GFX_SCR2, 0, 0);
    ngpc_gfx_clear(GFX_SCR1);
    ngpc_gfx_clear(GFX_SCR2);

    /* 3. Sprites : tout cacher (player/enemy/HUD popups precedents). */
    ngpc_sprite_hide_all();

    /* 4. Background hors-window = noir. */
    ngpc_gfx_set_bg_color(RGB(0, 0, 0));

    /* 5. (Re)installe la palette font sur SPR. Necessaire pour que les
     *    sprites Press A soient visibles -- sans ca, le slot peut avoir
     *    ete corrompu (palette 14 = quoi qu'il y avait au boot ou
     *    derniere ecriture). */
    install_font_palette_spr();

    /* 6. Tilemap GAME OVER sur SCR1. NGP_TILEMAP_BLIT_SCR1 charge tiles +
     *    palettes + map (pattern array+loop pour les palettes, donc safe
     *    vis-a-vis du bug cc900 "4 RGB() immediates"). */
    NGP_TILEMAP_BLIT_SCR1(game_over_screen, GAME_OVER_TILE_BASE);

    s_game_over_delay_timer = 0u;
    s_game_over_blink_timer = 0u;
    s_game_over_text_visible = 0u;

    /* SAVE-2 : reset RAM (bonus + unlocks sauf slot 0) + commit flash.
     * Place a la fin de game_over_init = etat video stable (raster disabled,
     * sprites hidden, scroll resetes), pattern equivalent StarGunner
     * options_leave qui save dans une state machine transition stable.
     * Commit EARLY ici plutot que dans game_over_update PAD_A pour qu'une
     * coupure console pendant le screen "Press A" ne perde pas le save. */
    {
        u8 i;
        player_state_reset();
        for (i = 1u; i < 4u; i++) s_level_select_unlocked[i] = 0u;
        mkd_save_commit();
    }
}

/* =========================================================================
 * MKD-depth : STATE_VICTORY (stats du run) + STATE_UPGRADE_CHOICE (3 cards)
 * =========================================================================
 *
 * Flow : descent au depth_target -> STATE_VICTORY (stats fixes, PAD_A
 * passe a UPGRADE) -> STATE_UPGRADE_CHOICE (LEFT/RIGHT pick + PAD_A applique
 * + retour STATE_MENU avec player_state_apply_powerup deja accumule).
 *
 * Les upgrades restent en RAM (session globale, accumule run apres run).
 * Au prochain dungeon_init, player_state_reset() NE serait PAS appele -
 * mais en pratique le menu START rappelle dungeon_init via STATE_DUNGEON
 * et le player_state.h conserve les bonus accumules tant qu'on ne call
 * pas player_state_reset(). */

#define VICTORY_INPUT_DELAY  20u  /* frames d'attente apres show avant PAD_A actif */

static u8 s_victory_delay_timer = 0u;
static u8 s_upgrade_selection = 0u;  /* 0=Force, 1=Vie, 2=Chance */
static u8 s_upgrade_blink_timer = 0u;
static u8 s_upgrade_input_delay = 0u;

static void victory_init(void)
{
    /* Reset video context comme game_over_init. */
    hud_raster_disable();
    ngpc_gfx_set_viewport(0, 0, SCREEN_W, SCREEN_H);
    HW_SCR_PRIO = 0x00;
    ngpc_gfx_scroll(GFX_SCR1, 0, 0);
    ngpc_gfx_scroll(GFX_SCR2, 0, 0);
    ngpc_gfx_clear(GFX_SCR1);
    ngpc_gfx_clear(GFX_SCR2);
    ngpc_sprite_hide_all();
    ngpc_gfx_set_bg_color(RGB(0, 0, 0));

    /* Font opaque (bg noir + lettre blanche). Reinstall en cas d'ecrasement
     * par dungeon (slot 416 ou palette 3 SCR2 ont pu etre overwritten). */
    hud_font_install_opaque_variant();
    hud_font_install_palettes(GFX_SCR2);

    /* Layout 20-col, rangees centrees verticalement. */
    hud_text_print    (GFX_SCR2, 2u,  3u, "DUNGEON CLEARED!");
    hud_text_print    (GFX_SCR2, 4u,  7u, "FLOOR");
    hud_text_print_num(GFX_SCR2, 12u, 7u, (u16)g_depth_current, 2u);
    hud_text_print    (GFX_SCR2, 4u,  8u, "KILLS");
    hud_text_print_num(GFX_SCR2, 12u, 8u, (u16)g_run_kills, 3u);
    hud_text_print    (GFX_SCR2, 4u,  9u, "TURNS");
    hud_text_print_num(GFX_SCR2, 12u, 9u, g_run_turns, 4u);
    hud_text_print    (GFX_SCR2, 4u, 10u, "SEED ");
    hud_text_print_num(GFX_SCR2, 12u, 10u, (u16)g_last_cluster_seed, 3u);

    /* MKD-depth : condition d'unlock du niveau suivant = 6 kills minimum
     * sur le run. Si remplie, retire le cadenas du slot 1 du level select
     * (s_level_select_unlocked[1] = 1u). Affiche un message different
     * selon que la condition est remplie ou non. */
    if (g_run_kills >= UNLOCK_KILLS_REQUIRED) {
        s_level_select_unlocked[1] = 1u;
        hud_text_print(GFX_SCR2, 2u, 12u, "NEXT UNLOCKED!");
    } else {
        hud_text_print(GFX_SCR2, 2u, 12u, "KILL 6 TO UNLOCK");
    }

    hud_text_print    (GFX_SCR2, 6u, 14u, "PRESS A");

    /* SAVE-2 : pas de save ici. Le save final fin-de-donjon est fait dans
     * upgrade_update APRES apply_powerup, ce qui evite de perdre le bonus
     * choisi en cas de coupure entre victory et upgrade choice. */

    s_victory_delay_timer = 0u;
}

static void victory_update(void)
{
    if (s_victory_delay_timer < (u8)VICTORY_INPUT_DELAY) {
        s_victory_delay_timer++;
        return;
    }
    if (ngpc_pad_pressed & PAD_A) {
        s_state = STATE_UPGRADE_CHOICE;
    }
}

static void upgrade_draw_cursor(void)
{
    /* Cursor = ">" devant la rangee selectionnee. Efface les 3 rangees
     * d'abord pour rafraichir uniquement la bonne. */
    hud_text_print(GFX_SCR2, 2u, 7u,  " ");
    hud_text_print(GFX_SCR2, 2u, 8u,  " ");
    hud_text_print(GFX_SCR2, 2u, 9u,  " ");
    hud_text_print(GFX_SCR2, 2u, (u8)(7u + s_upgrade_selection), ">");
}

static void upgrade_init(void)
{
    /* On NE clear PAS la VRAM ici : on remplace juste le contenu texte
     * pour passer du stats screen au upgrade screen. Le user a confirme
     * 2 ecrans separes mais le reset video complet n'est pas necessaire
     * entre les deux (memes palettes/font/scroll). */
    ngpc_gfx_clear(GFX_SCR2);
    hud_font_install_opaque_variant();
    hud_font_install_palettes(GFX_SCR2);

    hud_text_print(GFX_SCR2, 3u,  3u, "CHOOSE UPGRADE");

    hud_text_print(GFX_SCR2, 4u,  7u, "FORCE  +2 ATK");
    hud_text_print(GFX_SCR2, 4u,  8u, "VIE   +10 HP");
    hud_text_print(GFX_SCR2, 4u,  9u, "CHANCE +5%CRIT");

    hud_text_print(GFX_SCR2, 1u, 14u, "L/R PICK  A OK");

    s_upgrade_selection = 0u;
    s_upgrade_blink_timer = 0u;
    s_upgrade_input_delay = (u8)VICTORY_INPUT_DELAY;
    upgrade_draw_cursor();
}

static void upgrade_update(void)
{
    if (s_upgrade_input_delay > 0u) {
        s_upgrade_input_delay--;
        return;
    }

    if (ngpc_pad_pressed & (PAD_UP | PAD_LEFT)) {
        if (s_upgrade_selection > 0u) s_upgrade_selection--;
        else s_upgrade_selection = 2u;
        upgrade_draw_cursor();
    } else if (ngpc_pad_pressed & (PAD_DOWN | PAD_RIGHT)) {
        if (s_upgrade_selection < 2u) s_upgrade_selection++;
        else s_upgrade_selection = 0u;
        upgrade_draw_cursor();
    } else if (ngpc_pad_pressed & PAD_A) {
        u8 powerup_type;
        switch (s_upgrade_selection) {
        case 0u: powerup_type = (u8)POWERUP_ATK;    break;  /* Force */
        case 1u: powerup_type = (u8)POWERUP_HP_MAX; break;  /* Vie */
        default: powerup_type = (u8)POWERUP_CRIT;   break;  /* Chance */
        }
        player_state_apply_powerup(powerup_type);
        /* SAVE-2 : commit la save APRES apply_powerup pour que le bonus
         * choisi soit inclus en flash (sinon le user pourrait perdre 1
         * upgrade en eteignant entre victory_init et ici). */
        mkd_save_commit();
        /* Retour directement au level select : permet de relancer un run
         * sans repasser par le menu principal (bonus + unlocks deja en RAM). */
        s_state = STATE_LEVEL_SELECT;
    }
}

static void game_over_update(void)
{
    u8 want_visible;

    /* PAD_A -> retour menu + nouvelle partie de zero. Le menu relance
     * STATE_ROOM via Start -> room_init -> player_reset / enemy_place
     * (HP plein, position depart), mais on doit forcer :
     *   - retour STATIC_ROOM_01 (sinon spawn dans la derniere salle vue)
     *   - reset cluster dungeon (sinon ancien cluster persiste)
     *   - HP plein des maintenant (room_init le refera, mais le menu
     *     pourrait afficher la valeur entre les deux). */
    if (ngpc_pad_pressed & PAD_A) {
        s_static_room = STATIC_ROOM_01;
        s_static_room_entry = ROOM_ENTRY_DEFAULT;
        /* SAVE-2 : player_state_reset() + reset unlocks + flash save ont DEJA
         * tourne dans game_over_init (early commit pour survivre coupure
         * console pendant le screen "Press A"). Ici juste la transition. */
        s_cluster_count = 0u;
        game_over_hide_press_a();
        s_state = STATE_MENU;
        return;
    }

    /* Phase 1 : attente 2 sec apres affichage du game over screen. */
    if (s_game_over_delay_timer < (u8)GAME_OVER_TEXT_DELAY) {
        s_game_over_delay_timer++;
        return;
    }

    /* Phase 2 : "Press A" clignotant. Cycle = 2 * BLINK_HALF frames :
     * premiere moitie visible, seconde moitie cache. */
    s_game_over_blink_timer++;
    if (s_game_over_blink_timer >= (u8)(GAME_OVER_BLINK_HALF * 2u)) {
        s_game_over_blink_timer = 0u;
    }
    want_visible = (s_game_over_blink_timer < (u8)GAME_OVER_BLINK_HALF) ? 1u : 0u;

    if (want_visible && !s_game_over_text_visible) {
        game_over_draw_press_a();
        s_game_over_text_visible = 1u;
    } else if (!want_visible && s_game_over_text_visible) {
        game_over_hide_press_a();
        s_game_over_text_visible = 0u;
    }
}


/* ---- Main entry point ---- */

/* HUD font opaque : 2eme version de la font dupliquee a un slot VRAM
 * different (NGPC_FONT_TILE_BASE + 96), avec tile pixels transformes
 * (+0x5555 sur chaque word) pour avoir bg opaque (index 1) + letter
 * (index 2). Combine avec une palette dediee (C1=BLACK, C2=WHITE).
 *
 * Popups damage continuent a utiliser la font originale (slot 32, palette
 * 0 transparente) -> ils restent visuellement transparents.
 * HUD/pause/options utilisent la version opaque via hud_text_print*. */
/* Slot VRAM dedie a la font opaque. 416..511 = 96 tiles, apres tous les
 * autres assets (hud_mspr 400-408, map_* 400-402). 416 evite les conflits
 * avec PAUSE_TILE_BASE=128, lime_sheet=128, et tous les sprites du dungeon. */
#define HUD_FONT_TILE_BASE    416u
/* PAL slot 3 sur SCR2 : evite collision avec slot 0 (font transparent),
 * slot 1 (PAL_DECO dungeon), slot 2 (HUD_BG_PAL_SCR2 du bandeau hud_bg). */
#define HUD_FONT_PAL_SLOT     3u

static void hud_font_install_opaque_variant(void)
{
    u16 i;
    /* Copie l'original (deja charge a NGPC_FONT_TILE_BASE) vers le slot
     * HUD_FONT_TILE_BASE + transformation +0x5555. */
    for (i = 0u; i < (u16)((u16)NGPC_FONT_TILE_COUNT * 8u); i++) {
        u16 src_addr = (u16)((u16)NGPC_FONT_TILE_BASE * 8u + i);
        u16 dst_addr = (u16)(HUD_FONT_TILE_BASE * 8u + i);
        HW_TILE_DATA[dst_addr] = (u16)(HW_TILE_DATA[src_addr] + 0x5555u);
    }
}

/* Configure les 2 palettes font sur un plane (SCR1 ou SCR2) :
 *   slot 0 = TRANSPARENT (C1=WHITE) pour la font originale (popups, etc.)
 *   slot HUD_FONT_PAL_SLOT = OPAQUE (C1=BLACK, C2=WHITE) pour la variante HUD */
static void hud_font_install_palettes(u8 plane)
{
    /* Slot 0 = font transparente standard (popups, salle text generale) :
     *   C0 transparent, C1 = WHITE (letter, font originale a slot 32) */
    ngpc_gfx_set_palette(plane, 0u,
        RGB(0, 0, 0), RGB(15, 15, 15), RGB(0, 0, 0), RGB(0, 0, 0));
    /* Slot HUD_FONT_PAL_SLOT (3) = font opaque HUD :
     *   C0 unused, C1 = BLACK (bg patched index 1), C2 = WHITE (letter index 2) */
    ngpc_gfx_set_palette(plane, HUD_FONT_PAL_SLOT,
        RGB(0, 0, 0), RGB(0, 0, 0), RGB(15, 15, 15), RGB(0, 0, 0));
}

/* Text rendering avec font opaque (bg noir). Mirror minimaliste de
 * ngpc_text_print mais utilise HUD_FONT_TILE_BASE et HUD_FONT_PAL_SLOT. */
static void hud_text_print(u8 plane, u8 x, u8 y, const char *str)
{
    u8 cx = x;
    while (*str) {
        u8 ch = (u8)*str;
        if (ch >= 0x20u && ch < 0x80u) {
            ngpc_gfx_put_tile(plane, cx, y,
                (u16)((u16)HUD_FONT_TILE_BASE + (u16)(ch - 0x20u)),
                HUD_FONT_PAL_SLOT);
        }
        cx++;
        str++;
    }
}

static void hud_text_print_num(u8 plane, u8 x, u8 y, u16 value, u8 digits)
{
    u8 buf[6];
    u8 i;
    u16 v = value;
    if (digits > 5u) digits = 5u;
    buf[digits] = 0u;
    for (i = digits; i > 0u; i--) {
        buf[i - 1u] = (u8)('0' + (u8)(v % 10u));
        v = (u16)(v / 10u);
    }
    hud_text_print(plane, x, y, (const char *)buf);
}

void main(void)
{
    GameState prev_state;

    ngpc_init();
    /* Init player state global (HP max, ATK/DEF, inventaire vide). Avant
     * tout init UI pour que les widgets HUD aient des valeurs valides. */
    player_state_reset();
    /* SAVE-2 : init flash + load save si presente et valide. Apres
     * player_state_reset (qui pose les bases) pour overrider proprement.
     * No-op si NGP_ENABLE_FLASH_SAVE=0 (build.bat SET FlashSave=0).
     * Pattern shmup_profile_init de StarGunner_save_lib_test. */
    mkd_save_init();
    ahchay_font_load();
    ahchay_font_set_palette(GFX_SCR1, 0u);
    ahchay_font_set_palette(GFX_SCR2, 0u);
    /* MKD-misc : duplique la font en variante opaque a HUD_FONT_TILE_BASE +
     * installe la palette opaque sur SCR2 slot HUD_FONT_PAL_SLOT. */
    hud_font_install_opaque_variant();
    hud_font_install_palettes(GFX_SCR2);
    hud_font_install_palettes(GFX_SCR1);
    Sounds_Init();

    /* HUD split v4 : ISR Timer0 mini (isr_hud_split) + push shadow + arm timer
     * dans main loop juste apres ngpc_vsync(). Cf bloc autour de la ligne 320.
     *
     * Etapes boot :
     *  1. ngpc_raster_init() fait le BIOS_INTLVSET pour activer l'IRQ
     *     Timer0 niveau 4 (le seul moyen sur NGPC, cf 8Bit.txt H-int Setting).
     *     Il pose aussi HW_INT_TIM0 = isr_hblank et demarre le timer.
     *  2. ngpc_raster_disable() stoppe le timer (sinon isr_hblank fire chaque
     *     scanline et tue le FPS).
     *  3. Override HW_INT_TIM0 vers notre ISR mini (2 writes + stop). */
    ngpc_raster_init();
    ngpc_raster_disable();
    HW_INT_TIM0 = (IntHandler *)isr_hud_split;

    prev_state = STATE_MENU; /* force init on first frame */

    while (1) {
        ngpc_vsync();
        /* HUD split v4 : pousser shadow scroll + armer Timer0 PILE apres vsync.
         * On est en scanline ~0 ; TREG0=136 fire au split a scanline 136.
         * Si flag inactif (etats hors gameplay), on ne touche a rien =>
         * les ngpc_gfx_scroll/HW_SCR_PRIO des autres etats restent valides. */
        if (s_dung_hud_split_armed) {
            hud_apply_scroll_and_arm();
        }
        ngpc_input_update();
        Sounds_Update();

        if (s_state != prev_state) {
            GameState from = prev_state;
            prev_state = s_state;
            switch (s_state) {
            case STATE_INTRO: intro_init(); break;
            case STATE_MENU: menu_init(); break;
            case STATE_LEVEL_SELECT: level_select_init(); break;
            case STATE_ROOM:
                if (from == STATE_PAUSE) {
                    room_resume();
                } else {
                    room_init();
                }
                break;
            case STATE_PAUSE: pause_init(); break;
            case STATE_DUNGEON:
                /* MKD-5/8 : si on revient depuis PAUSE/MINIMAP/OPTIONS,
                 * reload VRAM seulement (NE PAS regen le cluster). */
                if (from == STATE_PAUSE || from == STATE_MINIMAP || from == STATE_OPTIONS) {
                    dungeon_resume();
                } else {
                    dungeon_init();
                }
                break;
            case STATE_GAME_OVER: game_over_init(); break;
            case STATE_MINIMAP: minimap_init(); break;  /* MKD-5 */
            case STATE_OPTIONS: options_init(); break;  /* MKD-8 v2 */
            case STATE_VICTORY: victory_init(); break;
            case STATE_UPGRADE_CHOICE: upgrade_init(); break;
            case STATE_INVENTORY: inventory_init(); break;  /* MKD-inv */
            }
        }

        /* Avance la sequence de mort en parallele du gameplay : pendant
         * phase 1 (attente 2s) et phase 2 (fade BG), la scene continue
         * d'etre dessinee mais room_update/dungeon_update ignorent leur
         * logique (cf check s_death_phase au debut). */
        update_death_sequence();

        switch (s_state) {
        case STATE_INTRO: intro_update(); break;
        case STATE_MENU: menu_update(); break;
        case STATE_LEVEL_SELECT: level_select_update(); break;
        case STATE_ROOM: room_update(); break;
        case STATE_PAUSE: pause_update(); break;
        case STATE_DUNGEON: dungeon_update(); break;
        case STATE_GAME_OVER: game_over_update(); break;
        case STATE_MINIMAP: minimap_update(); break;  /* MKD-5 */
        case STATE_OPTIONS: options_update(); break;  /* MKD-8 v2 */
        case STATE_VICTORY: victory_update(); break;
        case STATE_UPGRADE_CHOICE: upgrade_update(); break;
        case STATE_INVENTORY: inventory_update(); break;  /* MKD-inv */
        }
    }
}
