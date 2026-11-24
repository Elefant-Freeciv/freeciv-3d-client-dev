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

/***********************************************************************
  voxel_art.c - voxel terrain art catalogue.

  Port of the TILES_META / EDGE_PROFILES / plinth tables of the
  "Freeciv Isometric Voxel Terrain Studio" art page, plus the ruleset
  to art mapping the 3D map renderer needs.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <string.h>
#include <math.h>
#include <ctype.h>

/* utility */
#include "shared.h"

/* common */
#include "map.h"
#include "terrain.h"

/* client/gui-vulkan */
#include "voxel_art.h"

const char *const voxel_art_names[VOX_ART_COUNT] = {
  "forest", "meadow", "plains", "jungle", "hills", "mountains",
  "desert", "swamp", "tundra", "ice", "coast", "lake", "ocean",
  "city", "ancient", "medieval", "homestead", "watermill", "redbarn"
};

/* Straight from the EDGE_PROFILES table of the art page. */
const struct voxel_profile voxel_profiles[VOX_ART_COUNT] = {
  [VOX_ART_FOREST]    = { 0.26f, { "#558b2f", "#689f38", "#4d7c2b" }, 3, VOX_PLINTH_SOIL,   0.00f, FALSE },
  [VOX_ART_MEADOW]    = { 0.40f, { "#8bc34a", "#7cb342", "#9ccc65" }, 3, VOX_PLINTH_SOIL,   0.00f, FALSE },
  [VOX_ART_PLAINS]    = { 0.30f, { "#b8a040", "#a39338", "#cbb252" }, 3, VOX_PLINTH_SOIL,   0.00f, FALSE },
  [VOX_ART_JUNGLE]    = { 0.28f, { "#2e6b34", "#387d3e", "#245929" }, 3, VOX_PLINTH_SOIL,   0.00f, FALSE },
  [VOX_ART_HILLS]     = { 0.36f, { "#7cb342", "#689f38", "#d5bc8f" }, 3, VOX_PLINTH_SOIL,   0.00f, FALSE },
  [VOX_ART_MOUNTAINS] = { 0.36f, { "#545b63", "#4c8c2b", "#727a84" }, 3, VOX_PLINTH_SOIL,   0.00f, FALSE },
  [VOX_ART_DESERT]    = { 0.34f, { "#edd5a7", "#e2c38d", "#c87d4b" }, 3, VOX_PLINTH_SAND,   0.00f, FALSE },
  [VOX_ART_SWAMP]     = { 0.26f, { "#4a5d2e", "#3e6347", "#4e4431" }, 3, VOX_PLINTH_SWAMP,  0.22f, FALSE },
  [VOX_ART_TUNDRA]    = { 0.32f, { "#7a8a4b", "#eef4f8", "#a64d32" }, 3, VOX_PLINTH_SOIL,   0.00f, FALSE },
  [VOX_ART_ICE]       = { 0.38f, { "#eef6fc", "#b3e5fc", "#29b6f6" }, 3, VOX_PLINTH_ICE,    0.24f, FALSE },
  [VOX_ART_COAST]     = { 0.16f, { "#26c6da", "#64e5ee", "#e6cf9e" }, 3, VOX_PLINTH_SAND,   0.16f, TRUE  },
  [VOX_ART_LAKE]      = { 0.24f, { "#689f38", "#4e7c29", "#0288d1" }, 3, VOX_PLINTH_SOIL,   0.18f, TRUE  },
  [VOX_ART_OCEAN]     = { 0.16f, { "#1565c0", "#1e88e5", "#0d47a1" }, 3, VOX_PLINTH_OCEAN,  0.16f, TRUE  },
  [VOX_ART_CITY]      = { 0.24f, { "#b5bac0", "#9ea5ad", "#78909c" }, 3, VOX_PLINTH_SOIL,   0.00f, FALSE },
  [VOX_ART_ANCIENT]   = { 0.24f, { "#e6d3ab", "#dcc698", "#c8b084" }, 3, VOX_PLINTH_SAND,   0.00f, FALSE },
  [VOX_ART_MEDIEVAL]  = { 0.24f, { "#558b2f", "#7b8085", "#689f38" }, 3, VOX_PLINTH_SOIL,   0.00f, FALSE },
  [VOX_ART_HOMESTEAD] = { 0.25f, { "#7cb342", "#8bc34a", "#b89a70" }, 3, VOX_PLINTH_SOIL,   0.00f, FALSE },
  [VOX_ART_WATERMILL] = { 0.25f, { "#7cb342", "#689f38", "#95a5a6" }, 3, VOX_PLINTH_SOIL,   0.00f, FALSE },
  [VOX_ART_REDBARN]   = { 0.25f, { "#7cb342", "#5e9e3c", "#a88a64" }, 3, VOX_PLINTH_SOIL,   0.00f, FALSE }
};

/* addStandardPlinth() of the art page: three stacked slabs under y = 0. */
const struct voxel_plinth_layer
  voxel_plinths[VOX_PLINTH_COUNT][3] = {
  [VOX_PLINTH_SOIL] = {
    { -1.05f, 0.40f, "#462c1c" }, { -0.65f, 0.40f, "#6a442a" },
    { -0.225f, 0.45f, "#8a5d3b" }
  },
  [VOX_PLINTH_SAND] = {
    { -1.05f, 0.40f, "#8b6843" }, { -0.65f, 0.40f, "#b58d58" },
    { -0.225f, 0.45f, "#d8b77e" }
  },
  [VOX_PLINTH_OCEAN] = {
    { -1.05f, 0.40f, "#0d2235" }, { -0.65f, 0.40f, "#13314c" },
    { -0.225f, 0.45f, "#1a4263" }
  },
  [VOX_PLINTH_ICE] = {
    { -1.05f, 0.40f, "#15324d" }, { -0.65f, 0.40f, "#23537a" },
    { -0.225f, 0.45f, "#3a7ca5" }
  },
  [VOX_PLINTH_SWAMP] = {
    { -1.05f, 0.40f, "#2a241b" }, { -0.65f, 0.40f, "#3d3626" },
    { -0.225f, 0.45f, "#4f4a32" }
  }
};

const char *const voxel_improvement_names[VOX_IMP_COUNT] = {
  "road", "railroad", "maglev", "river", "canal", "bridge",
  "irrigation", "farmland", "mine", "oil_well", "oil_platform",
  "fort", "fortress", "castle", "bunker", "airstrip", "airbase",
  "buoy", "hideout", "village", "ruins", "pollution", "fallout"
};

/**********************************************************************//**
  mulberry32() of the art page - small, fast and reproducible so that a
  tile always grows the same trees no matter when it is (re)meshed.
**************************************************************************/
uint32_t voxel_rand_next(uint32_t *state)
{
  uint32_t t = (*state += 0x6D2B79F5u);

  t = (t ^ (t >> 15)) * (t | 1u);
  t ^= t + ((t ^ (t >> 7)) * (t | 61u));
  return t ^ (t >> 14);
}

/**********************************************************************//**
  Random float in [0,1).
**************************************************************************/
float voxel_rand_float(uint32_t *state)
{
  return (float) voxel_rand_next(state) / 4294967296.0f;
}

/**********************************************************************//**
  sRGB #rrggbb to linear RGB, like convertSRGBToLinear() on the page.
**************************************************************************/
void voxel_color_linear(const char *hex, float out[3])
{
  for (int i = 0; i < 3; i++) {
    int value = 0;
    const char *p = hex + 1 + i * 2;

    for (int n = 0; n < 2 && *p != '\0'; n++, p++) {
      int digit = isdigit((unsigned char) *p)
                    ? (*p - '0')
                    : (tolower((unsigned char) *p) - 'a' + 10);
      if (digit < 0 || digit > 15) {
        digit = 0;
      }
      value = value * 16 + digit;
    }

    float c = (float) value / 255.0f;
    out[i] = c <= 0.04045f ? c / 12.92f
                           : powf((c + 0.055f) / 1.055f, 2.4f);
  }
}

/**********************************************************************//**
  Palette colour of an art profile.
**************************************************************************/
void voxel_profile_color(const struct voxel_profile *profile, int index,
                         float out[3])
{
  if (index < 0 || index >= profile->color_count) {
    index = 0;
  }
  voxel_color_linear(profile->colors[index], out);
}

/**********************************************************************//**
  isLandId() of the art page.
**************************************************************************/
bool voxel_art_is_land(enum voxel_art_id id)
{
  if (id >= VOX_ART_COUNT) {
    return false;
  }
  return !voxel_profiles[id].is_water;
}

/**********************************************************************//**
  blendCellWithNeighbors() of the art page: pull the height of one art
  cell towards the profiles of the four neighbouring tiles and dither
  the palette across the seam.

  gx/gz are cell coordinates inside the tile (0 .. VOX_CELLS_PER_TILE-1).
  feather is the blend radius in cells (the page's "Feather" slider).
  On return *height holds the blended height and *color_index the palette
  entry to use for the cell.
**************************************************************************/
void voxel_blend_cell(enum voxel_art_id self,
                      enum voxel_art_id north, enum voxel_art_id east,
                      enum voxel_art_id south, enum voxel_art_id west,
                      int gx, int gz, int feather,
                      float *height, int *color_index)
{
  struct { int dist; enum voxel_art_id nb; } checks[4] = {
    { gz,                       north },
    { VOX_CELLS_PER_TILE - gz,  south },
    { gx,                       west  },
    { VOX_CELLS_PER_TILE - gx,  east  }
  };
  uint32_t rng_state;
  float h = *height;

  if (feather < 1) {
    feather = 1;
  }

  /* Same cell -> same blend result every time it is rebuilt. */
  rng_state = (uint32_t) (self * 7919 + gx * 104729 + gz * 15485863);
  voxel_rand_next(&rng_state);

  for (int i = 0; i < 4; i++) {
    const struct voxel_profile *profile;

    if (checks[i].nb >= VOX_ART_COUNT || checks[i].dist >= feather) {
      continue;
    }

    profile = &voxel_profiles[checks[i].nb];

    /* Land/water edges get a softer, wider transition than seams
     * between two land biomes. */
    float weight = ((float) (feather - checks[i].dist) / feather) * 0.55f;
    if (voxel_art_is_land(self) != voxel_art_is_land(checks[i].nb)) {
      weight *= 1.35f;
    }
    if (weight > 1.0f) {
      weight = 1.0f;
    }

    h = h * (1.0f - weight) + profile->height * weight;

    if (voxel_rand_float(&rng_state) < weight * 1.15f) {
      *color_index = (int) (voxel_rand_float(&rng_state)
                            * profile->color_count);
      if (*color_index >= profile->color_count) {
        *color_index = profile->color_count - 1;
      }
    }
  }

  if (h < 0.14f) {
    h = 0.14f;
  }
  *height = h;
}

/**********************************************************************//**
  Match a ruleset terrain name against the art catalogue. The ruleset is
  free to call its terrains anything, so look for the well known
  substrings first and fall back on the ocean flag.
**************************************************************************/
static enum voxel_art_id art_from_name(const char *name, bool ocean)
{
  char buf[128];
  int i;

  if (name == nullptr || name[0] == '\0') {
    return ocean ? VOX_ART_OCEAN : VOX_ART_MEADOW;
  }

  strncpy(buf, name, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  for (i = 0; buf[i] != '\0'; i++) {
    buf[i] = (char) tolower((unsigned char) buf[i]);
  }

  struct { const char *needle; enum voxel_art_id id; } table[] = {
    { "forest",    VOX_ART_FOREST },
    { "jungle",    VOX_ART_JUNGLE },
    { "swamp",     VOX_ART_SWAMP },
    { "marsh",     VOX_ART_SWAMP },
    { "desert",    VOX_ART_DESERT },
    { "tundra",    VOX_ART_TUNDRA },
    { "glacier",   VOX_ART_ICE },
    { "ice",       VOX_ART_ICE },
    { "mountain",  VOX_ART_MOUNTAINS },
    { "peak",      VOX_ART_MOUNTAINS },
    { "volcan",    VOX_ART_MOUNTAINS },
    { "hill",      VOX_ART_HILLS },
    { "plains",    VOX_ART_PLAINS },
    { "steppe",    VOX_ART_PLAINS },
    { "grass",     VOX_ART_MEADOW },
    { "meadow",    VOX_ART_MEADOW },
    { "lake",      VOX_ART_LAKE },
    { "coast",     VOX_ART_COAST },
    { "shore",     VOX_ART_COAST },
    { "ocean",     VOX_ART_OCEAN },
    { "sea",       VOX_ART_OCEAN },
    { "deep",      VOX_ART_OCEAN },
    { nullptr,     VOX_ART_COUNT }
  };

  for (i = 0; table[i].needle != nullptr; i++) {
    if (strstr(buf, table[i].needle) != nullptr) {
      return table[i].id;
    }
  }

  return ocean ? VOX_ART_OCEAN : VOX_ART_MEADOW;
}

/**********************************************************************//**
  Pick the art recipe for a map tile.
**************************************************************************/
enum voxel_art_id voxel_art_for_tile(const struct tile *ptile)
{
  const struct terrain *pterrain = tile_terrain(ptile);
  bool ocean;

  if (pterrain == nullptr) {
    return VOX_ART_OCEAN;
  }

  ocean = is_ocean(pterrain);

  /* City scenery is part of the decoration layer (PLAN.md milestone M5);
   * the ground itself keeps its terrain art. */
  return art_from_name(terrain_name_translation(pterrain), ocean);
}

/**********************************************************************//**
  Map a ruleset extra onto one of the improvement models. The ruleset
  names of the classic extras match the studio page catalogue, so a name
  lookup covers the common cases; flag based matching is a TODO for the
  milestone that implements each model (see PLAN.md).
**************************************************************************/
int voxel_improvement_for_extra(const struct extra_type *pextra)
{
  const char *name;
  char buf[128];
  int i, j;

  if (pextra == nullptr) {
    return -1;
  }

  name = extra_name_translation(pextra);
  if (name == nullptr) {
    return -1;
  }

  strncpy(buf, name, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  for (i = 0; buf[i] != '\0'; i++) {
    buf[i] = (char) tolower((unsigned char) buf[i]);
  }

  for (i = 0; i < VOX_IMP_COUNT; i++) {
    size_t len = strlen(voxel_improvement_names[i]);

    if (len > 0 && strncmp(buf, voxel_improvement_names[i], len) == 0) {
      return i;
    }
  }

  /* Some rulesets use spaced or capitalised names. */
  struct { const char *needle; int id; } table[] = {
    { "railroad",   VOX_IMP_RAILROAD },
    { "maglev",     VOX_IMP_MAGLEV },
    { "river",      VOX_IMP_RIVER },
    { "canal",      VOX_IMP_CANAL },
    { "irrigat",    VOX_IMP_IRRIGATION },
    { "mine",       VOX_IMP_MINE },
    { "oil well",   VOX_IMP_OIL_WELL },
    { "oil platform", VOX_IMP_OIL_PLATFORM },
    { "fortress",   VOX_IMP_FORTRESS },
    { "fort",       VOX_IMP_FORT },
    { "castle",     VOX_IMP_CASTLE },
    { "airbase",    VOX_IMP_AIRBASE },
    { "pollution",  VOX_IMP_POLLUTION },
    { "fallout",    VOX_IMP_FALLOUT },
    { "ruins",      VOX_IMP_RUINS },
    { "village",    VOX_IMP_VILLAGE },
    { "hut",        VOX_IMP_VILLAGE },
    { nullptr,      -1 }
  };

  for (j = 0; table[j].needle != nullptr; j++) {
    if (strstr(buf, table[j].needle) != nullptr) {
      return table[j].id;
    }
  }

  return -1;
}
