/*
 * gui-irrlicht: Phase 5 3D map view.
 *
 * Ports the demo's merged-mesh terrain renderer (Main.cpp) but drives it from
 * FreeCiv's real client map (wld.map) instead of a hardcoded ASCII map. The
 * static terrain is one merged SMesh (one draw call); fog-of-war tiles are
 * simply omitted; picking is an analytical ray->y=0-plane intersection (the
 * Irrlicht collision manager skips invisible nodes, so no collision plane is
 * needed).
 *
 * Internal to gui-irrlicht (C++ linkage, not part of the FreeCiv gui_funcs
 * vtable). Called from gui_main.cpp on the main thread.
 */
#ifndef IRG_MAP3D_H
#define IRG_MAP3D_H

/* Build the 3D terrain mesh from the client's real map. Call once, after the
 * map is available (C_S_RUNNING). Returns 1 on success, 0 if the map isn't
 * ready or nothing could be rendered. */
int  irrg_map3d_build(void);
int  irrg_map3d_is_built(void);
/* Render the 3D scene to the window (does beginScene/drawAll/endScene). */
void irrg_map3d_render(void);
/* Draw the 3D scene without its own beginScene/endScene (for compositing a
 * 2D overlay in a single scene). */
void irrg_map3d_draw3d(void);
/* Sync the unit billboards (camera-facing, using the tileset unit sprite) and
 * the glowing, animated selection ring to the current state of the player's
 * units + the focused (selected) unit. Call each frame in the 3D path, before
 * irrg_map3d_draw3d(), so smgr->drawAll() renders them. */
void irrg_map3d_draw_units(void);
/* Sync the city + resource billboards (the player's cities + explored-tile
 * bonuses/ore, using the tileset sprites) to the current state. Call each
 * frame in the 3D path, after irrg_map3d_draw_units() and before
 * irrg_map3d_draw3d(), so smgr->drawAll() renders them. */
void irrg_map3d_draw_cities_and_resources(void);
/* Rebuild the terrain mesh to include tiles revealed since the last build
 * (units exploring new land), without moving the camera or the billboards.
 * Call each frame in the 3D path (it is a cheap no-op until new tiles appear).
 * Returns 1 if it rebuilt. */
int  irrg_map3d_refresh_tiles(void);
/* Render the 3D scene into an offscreen render-target texture (sized to the
 * window) and return it. The texture is owned/cached by the 3D module and
 * valid until the next call; gui_main composites it + the 2D overlay in a
 * SINGLE present to avoid the flicker that a second, separate endScene caused.
 * Returns 0 if the scene isn't ready. */
void *irrg_map3d_render_scene(void);   /* video::ITexture* */
/* Pick the tile under a screen pixel via ray->y=0-plane intersection.
 * Returns 1 and fills *out_x/*out_y (map coords) on a hit. */
int  irrg_map3d_pick(int mx, int my, int *out_x, int *out_y);
/* Like irrg_map3d_pick but returns the UNROUNDED world-space (x, z) ground
 * point under the pixel, for smooth mouse grab-pan. Returns 1 on a hit. */
int  irrg_map3d_pick_world(int mx, int my, float *out_x, float *out_y);
/* Reposition the camera to look at map tile (tx, ty). */
void irrg_map3d_center_on(int tx, int ty);
void irrg_map3d_focus_unit(int tx, int ty);
void irrg_map3d_refocus_current_unit(void);
void irrg_map3d_pan(int dx, int dz);
void irrg_map3d_zoom(int delta);
void irrg_map3d_destroy(void);

#endif /* IRG_MAP3D_H */
