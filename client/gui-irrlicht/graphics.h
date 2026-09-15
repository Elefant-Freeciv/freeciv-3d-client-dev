/*
 * gui-irrlicht: core graphics types + device accessors.
 *
 * struct color / sprite / canvas are "gui-dep" opaque types (forward-declared
 * in include/canvas_g.h); this header gives them their concrete definitions.
 * Phase 2 keeps them minimal; Phase 4 adds the Irrlicht texture / pixel buffer
 * members used for real 2D rendering.
 */
#ifndef IRG_GRAPHICS_H
#define IRG_GRAPHICS_H

#include "support.h"   /* bool */

/* A FreeCiv color (RGB; alpha handled internally by the 2D layer). */
struct color {
  int r, g, b;
};

/* A loaded image. Phase 4: sprites are pure CPU SColor* buffers (A8R8G8B8).
 * We deliberately do NOT keep an irr::video::ITexture per sprite: the
 * Irrlicht texture cache owns a reference it never releases, so dropping our
 * copy to zero leaves a dangling SSurface entry that later findTexture()
 * dereferences (SIGSEGV). Loading goes straight to a CPU buffer via the
 * image loaders, and the only texture we manage is the canvas present target.
 * crop_x/crop_y record the region offset within the source sheet. */
struct sprite {
  int width, height;
  void *irr_tex;   /* always 0 (kept for ABI/struct compat) */
  void *pixels;    /* CPU SColor* (A8R8G8B8), width*height */
  int crop_x, crop_y;
};

/* An offscreen 2D drawing surface (dialogs render into this). */
struct canvas {
  int width, height;
  float zoom;
  bool is_mapview;
  void *pixels;    /* CPU front buffer, SColor* (A8R8G8B8) */
  void *irr_rtt;   /* cached present ITexture* (managed by irrg_canvas_present) */
};

/* Device / scene singletons (created in irrg_ui_main, see gui_main.cpp). */
void *irrg_device(void);   /* irr::IrrlichtDevice*   */
void *irrg_scmgr(void);    /* irr::scene::ISceneManager* */
void *irrg_vdriver(void);  /* irr::video::IVideoDriver*  */
void *irrg_guienv(void);   /* irr::gui::IGUIEnvironment* */

/* Present a canvas to the window (upload CPU buffer to a texture, draw2DImage).
 * gui-irrlicht internal (not part of the FreeCiv gui_funcs vtable). */
void irrg_canvas_present(struct canvas *c);
/* Like irrg_canvas_present but shows only a centered crop of the canvas, CPU
 * nearest-neighbor upscaled to fill (win_w x win_h). zoom>1 => show a smaller
 * central region (zoomed in). The capital is at the canvas center (the mapview
 * is centered on it), so it stays centered. Used for the 2D presentation zoom. */
void irrg_canvas_present_zoomed(struct canvas *c, float zoom,
                                int win_w, int win_h);
/* The main map-view canvas (captured in irrg_canvas_mapview_init), or 0. */
struct canvas *irrg_get_map_canvas(void);

/* Diagnose why the presented 2D map might be grey: count non-dark (terrain / water)
 * pixels in the *presented* crop and across the whole store, then log a verdict
 * (empty store = no data / fog; dark crop but bright store = view not centered on
 * the explored area; crop bright = OK). For debugging grey-map reports. */
void irrg_diagnose_present(const struct canvas *c, float zoom);

#endif /* IRG_GRAPHICS_H */
