/* Hand-optimized selector metasprite.
 * Source: GraphX/selecteur.png, reduced to one 8x8 corner tile and flips.
 */

#include "ngpc_types.h"
#include "ngpc_metasprite.h"

const u16 selecteur_tiles_count = 8u;
const u16 selecteur_tiles[] = {
    0x5540, 0x4000, 0x4000, 0x4000, 0x4000, 0x0000, 0x0000, 0x0000
};

const u8 selecteur_palette_count = 1u;
const u16 selecteur_palettes[] = {
    0x0000, 0x0000, 0x0000, 0x0000
};

const u8 selecteur_pal_base = 3u;

const u16 selecteur_tile_base = 160u;

const NgpcMetasprite selecteur_frame_0 = {
    4u, 16u, 16u,
    {
        { 0, 0, 160, 3, 0 },
        { 8, 0, 160, 3, SPR_HFLIP },
        { 0, 8, 160, 3, SPR_VFLIP },
        { 8, 8, 160, 3, SPR_HFLIP | SPR_VFLIP }
    }
};

const MsprAnimFrame selecteur_anim[] = {
    { &selecteur_frame_0, 8 }
};

const u8 selecteur_anim_count = 1u;
