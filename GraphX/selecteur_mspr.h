/* Hand-optimized selector metasprite. */

#ifndef SELECTEUR_MSPR_H
#define SELECTEUR_MSPR_H

#include "ngpc_types.h"
#include "ngpc_metasprite.h"

extern const u16 selecteur_tiles_count;
extern const u16 NGP_FAR selecteur_tiles[];

extern const u8 selecteur_palette_count;
extern const u16 NGP_FAR selecteur_palettes[];
extern const u8 selecteur_pal_base;
extern const u16 selecteur_tile_base;

extern const NgpcMetasprite selecteur_frame_0;

extern const MsprAnimFrame selecteur_anim[];
extern const u8 selecteur_anim_count;

#endif /* SELECTEUR_MSPR_H */
