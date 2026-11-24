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
  vk3d_map.c - the 3D map scene: camera, chunk cache and the draw pass.

  This is the skeleton the 3D renderer is built out of. The milestones
  in client/gui-vulkan/PLAN.md fill it in, in this order:

    M2  terrain chunk meshing from voxel_art.c + indexed draw
    M3  lighting, water plane, fog
    M4  camera, input, picking, transparent map viewport
    M5  decorations: cities, improvements, units, fog of war
    M6  animation, shadows, quality settings
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <math.h>
#include <string.h>

/* utility */
#include "log.h"

/* client/gui-vulkan */
#include "vk3d.h"
#include "vk3d_map.h"

/* Chunk = square block of map tiles that is meshed and uploaded as one
 * unit. 8x8 tiles keeps a rebuild cheap while amortising draw calls. */
#define VK3D_CHUNK_TILES 8

struct vk3d_map_state {
  struct vk3d_view view;
  bool dirty_all;
};

static struct vk3d_map_state map_state;

/**********************************************************************//**
  Default camera: the isometric three-quarter view of the art page
  (camera at (13.5, 11.5, 13.5) looking at the tile centre).
**************************************************************************/
void vk3d_map_view_default(struct vk3d_view *view)
{
  view->center_x = 0.0f;
  view->center_z = 0.0f;
  view->height = 11.5f;
  view->yaw = M_PI / 4.0f;
  view->pitch = 0.72f;
  view->fov_y = 36.0f * (M_PI / 180.0f);
}

/**********************************************************************//**
  Drop every cached chunk. Called on game start, tileset change and when
  the map topology is replaced.
**************************************************************************/
void vk3d_map_invalidate_all(void)
{
  map_state.dirty_all = TRUE;
}

/**********************************************************************//**
  A tile changed on the server: mark its chunk for remeshing.
**************************************************************************/
void vk3d_map_tile_dirty(struct tile *ptile)
{
  /* M2: map the tile to its chunk and set the chunk dirty bit. */
  (void) ptile;
}

/**********************************************************************//**
  Unit / city / extra decoration of a tile changed.
**************************************************************************/
void vk3d_map_tile_decor_dirty(struct tile *ptile)
{
  /* M5: decoration layer is instanced, so this only marks the chunk's
   * decoration buffer, not its terrain mesh. */
  (void) ptile;
}

/**********************************************************************//**
  Draw the map scene. Called inside the render pass, before the GUI layer.
**************************************************************************/
void vk3d_map_record(VkCommandBuffer cmd_buffer)
{
  /* M2 replaces this with the chunk draw loop:
   *   vkCmdBindPipeline(scene pipeline)
   *   vkCmdPushConstant(view/projection, light)
   *   for each visible chunk: vkCmdBindVertexBuffers + vkCmdDrawIndexed
   */
  (void) cmd_buffer;
}
