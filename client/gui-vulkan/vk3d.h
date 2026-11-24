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
#ifndef FC__VK3D_H
#define FC__VK3D_H

/* SDL3 */
#include <SDL3/SDL.h>

/* client */
#include "fc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * vk3d.h - Vulkan 3D map renderer front end for the gui-vulkan client.
 *
 * The client keeps the gui-sdl3 widget toolkit for all 2D GUI. That
 * toolkit draws into CPU surfaces (main_surface). vk3d owns the window
 * presentation: every frame it renders the 3D voxel map scene and then
 * composites the 2D GUI surface on top of it, so both live in the same
 * window without fighting over the swapchain.
 *
 * See client/gui-vulkan/PLAN.md for the full roadmap.
 */

/*
 * Initialize Vulkan for 'window'. The window MUST have been created with
 * the SDL_WINDOW_VULKAN flag. Returns FALSE if Vulkan is unavailable, in
 * which case the caller is expected to fall back to the plain SDL3
 * renderer path.
 */
bool vk3d_init(SDL_Window *window, int width, int height);

/* Destroy everything created by vk3d_init(). Safe to call when inactive. */
void vk3d_shutdown(void);

/* TRUE when vk3d owns window presentation. */
bool vk3d_is_active(void);

/* Window resized. Recreates the swapchain. */
void vk3d_resize(int width, int height);

/*
 * Render one frame: 3D map scene, then the 2D GUI layer from 'gui'
 * (an RGBA surface) composited over it, then present.
 */
bool vk3d_present(const SDL_Surface *gui);

/*
 * Tell the renderer that the GUI layer changed, so that it has to be
 * re-uploaded to the GPU before the next present.
 */
void vk3d_gui_dirty(void);


/* --- Map scene, see vk3d_map.c --------------------------------------- */

/* Camera / view state used by the map scene. */
struct vk3d_view {
  float center_x, center_z;   /* World position the camera looks at. */
  float height;               /* Camera height above the ground. */
  float yaw;                  /* Rotation around Z, radians. */
  float pitch;                /* Tilt from straight down, radians. */
  float fov_y;                /* Vertical field of view, radians. */
};

void vk3d_map_view_default(struct vk3d_view *view);

/* Mark all map geometry as needing a rebuild (new game, tileset change). */
void vk3d_map_invalidate_all(void);

/* Mark a single tile dirty after the server changed it. */
void vk3d_map_tile_dirty(struct tile *ptile);

/* Mark the unit/city/extras decoration layer of a tile dirty. */
void vk3d_map_tile_decor_dirty(struct tile *ptile);

#ifdef __cplusplus
}
#endif

#endif /* FC__VK3D_H */
