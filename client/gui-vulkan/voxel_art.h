/***********************************************************************
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__VOXEL_ART_H
#define FC__VOXEL_ART_H

/* client */
#include "fc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * voxel_art.h - the voxel terrain art catalogue.
 *
 * This is the C port of the art system of the "Freeciv Isometric Voxel
 * Terrain Studio" page (freeciv_isometric_voxel_terrain_studio_19_tiles_
 * improvements_glb.html). That page generates every tile as merged voxel
 * boxes with a deterministic RNG; the tables here carry the same art
 * direction into the client so the 3D map looks like the studio preview.
 *
 * World scale, same as the studio page:
 *   one map tile  = 10 x 10 world units wide, 20 x 20 art cells
 *   one art cell  = 0.5 x 0.5 units
 *   terrain height ~ 0.14 .. 0.6 units, plinth below y = 0
 */

/* Art ids of the 19 tile generators of the studio page. */
enum voxel_art_id {
  VOX_ART_FOREST = 0,
  VOX_ART_MEADOW,
  VOX_ART_PLAINS,
  VOX_ART_JUNGLE,
  VOX_ART_HILLS,
  VOX_ART_MOUNTAINS,
  VOX_ART_DESERT,
  VOX_ART_SWAMP,
  VOX_ART_TUNDRA,
  VOX_ART_ICE,
  VOX_ART_COAST,
  VOX_ART_LAKE,
  VOX_ART_OCEAN,
  VOX_ART_CITY,
  VOX_ART_ANCIENT,
  VOX_ART_MEDIEVAL,
  VOX_ART_HOMESTEAD,
  VOX_ART_WATERMILL,
  VOX_ART_REDBARN,
  VOX_ART_COUNT
};

/* Terrain height / palette / plinth recipe of one art id. Mirrors the
 * EDGE_PROFILES table of the studio page. Colours are stored as #rrggbb
 * strings and converted to linear RGB by voxel_color_linear(). */
struct voxel_profile {
  float height;              /* Top height of the terrain surface. */
  const char *colors[3];     /* Up to 3 dither palette colours. */
  int color_count;
  int plinth;                /* enum voxel_plinth. */
  float water_y;             /* Standing water plane, 0 = none. */
  bool is_water;
};

/* Plinth (the layered soil/sand base a tile is standing on). */
enum voxel_plinth {
  VOX_PLINTH_SOIL = 0,
  VOX_PLINTH_SAND,
  VOX_PLINTH_OCEAN,
  VOX_PLINTH_ICE,
  VOX_PLINTH_SWAMP,
  VOX_PLINTH_COUNT
};

struct voxel_plinth_layer {
  float center_y;
  float size_y;
  const char *color;
};

/* Terrain improvements / extras the studio page models. */
enum voxel_improvement {
  VOX_IMP_ROAD = 0,
  VOX_IMP_RAILROAD,
  VOX_IMP_MAGLEV,
  VOX_IMP_RIVER,
  VOX_IMP_CANAL,
  VOX_IMP_BRIDGE,
  VOX_IMP_IRRIGATION,
  VOX_IMP_FARMLAND,
  VOX_IMP_MINE,
  VOX_IMP_OIL_WELL,
  VOX_IMP_OIL_PLATFORM,
  VOX_IMP_FORT,
  VOX_IMP_FORTRESS,
  VOX_IMP_CASTLE,
  VOX_IMP_BUNKER,
  VOX_IMP_AIRSTRIP,
  VOX_IMP_AIRBASE,
  VOX_IMP_BUOY,
  VOX_IMP_HIDEOUT,
  VOX_IMP_VILLAGE,
  VOX_IMP_RUINS,
  VOX_IMP_POLLUTION,
  VOX_IMP_FALLOUT,
  VOX_IMP_COUNT
};

extern const char *const voxel_art_names[VOX_ART_COUNT];
extern const struct voxel_profile voxel_profiles[VOX_ART_COUNT];
extern const struct voxel_plinth_layer
       voxel_plinths[VOX_PLINTH_COUNT][3];
extern const char *const voxel_improvement_names[VOX_IMP_COUNT];

/* Studio page constants. */
#define VOX_TILE_UNITS        10.0f
#define VOX_CELL_UNITS         0.5f
#define VOX_CELLS_PER_TILE    20

/* Deterministic RNG used by the generators (mulberry32 of the page). */
uint32_t voxel_rand_next(uint32_t *state);
float voxel_rand_float(uint32_t *state);

/* Convert a #rrggbb string to linear RGB (the page converts sRGB->linear). */
void voxel_color_linear(const char *hex, float out[3]);

/* Colour 'i' of a profile, in linear RGB. */
void voxel_profile_color(const struct voxel_profile *profile, int index,
                         float out[3]);

/* TRUE for the art ids that are open water. */
bool voxel_art_is_land(enum voxel_art_id id);

/* Blend one art cell towards its N/E/S/W neighbours. Port of
 * blendCellWithNeighbors(); 'n'/'e'/'s'/'w' are art ids of the
 * neighbouring tiles, VOX_ART_COUNT means "unknown". */
void voxel_blend_cell(enum voxel_art_id self,
                      enum voxel_art_id north, enum voxel_art_id east,
                      enum voxel_art_id south, enum voxel_art_id west,
                      int gx, int gz, int feather,
                      float *height, int *color_index);

/* Map a ruleset terrain to one of the art ids above. */
enum voxel_art_id voxel_art_for_tile(const struct tile *ptile);

/* Map a ruleset extra to one of the improvement ids above, or -1. */
int voxel_improvement_for_extra(const struct extra_type *pextra);

#ifdef __cplusplus
}
#endif

#endif /* FC__VOXEL_ART_H */
