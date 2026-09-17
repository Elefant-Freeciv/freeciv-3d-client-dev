/*
 * gui-irrlicht: Civilization IV inspired colour palette + beveled 2-D drawing
 * helpers (shared by the in-game overlays, dialogs and the start-game menu).
 *
 * The look: dark charcoal panels framed by a thin tan/gold BEVEL (a lighter
 * gold along the top/left edges, a darker gold along the bottom/right), gold
 * title header bars, cream body text, and green research/OK accents. All the
 * drawing goes through the existing irrg_canvas_* primitives so it composites
 * exactly like the rest of the 2-D layer.
 */
#ifndef IRG_THEME_H
#define IRG_THEME_H

#include "graphics.h"   /* struct color {r,g,b}; struct canvas (concrete) */
#include "support.h"    /* bool */

struct canvas;

/* The Civ4 palette (const; read via irrg_civ()). */
struct irrg_civ_pal {
  struct color bg;        /* dark charcoal panel fill                 */
  struct color bg_dark;   /* darker inset (bar backgrounds)           */
  struct color gold;      /* border base                              */
  struct color gold_hi;   /* bevel highlight (top/left)               */
  struct color gold_lo;   /* bevel shadow (bottom/right)              */
  struct color hdr_top;   /* header bar (light gold)                  */
  struct color hdr_bot;   /* header bar (dark gold)                   */
  struct color hdr_tx;    /* header text (dark brown)                 */
  struct color text;      /* cream body text                          */
  struct color text_dim;  /* dim body text                            */
  struct color green;     /* research / OK accent                     */
  struct color green_lo;  /* darker green                             */
  struct color blue;      /* water / info accent                      */
  struct color red;       /* warning accent                           */
  struct color btn;       /* button fill                              */
  struct color btn_hi;    /* button fill (hover)                      */
};
struct irrg_civ_pal &irrg_civ(void);

/* A thin gold beveled border (no fill): highlight top/left, shadow bottom/right. */
void irrg_civ_frame(struct canvas *cv, int x, int y, int w, int h);

/* A beveled dark panel with an optional gold title header. Returns the y where
 * panel content should start (below the header when a title is given). */
int  irrg_civ_panel(struct canvas *cv, int x, int y, int w, int h, const char *title);

/* A beveled gold/dark button. `disabled` greys it out; `pressed` (default
 * false) gives a sunken (mouse-button-held) look. */
void irrg_civ_button(struct canvas *cv, int x, int y, int w, int h,
                     const char *label, bool hovered, bool disabled,
                     bool pressed = false);

/* A Civ4-style progress bar (dark inset + coloured fill + gold frame) with an
 * optional centred label. `frac` is clamped to [0,1]. */
void irrg_civ_bar(struct canvas *cv, int x, int y, int w, int h,
                  double frac, struct color *fg, const char *center);

#endif /* IRG_THEME_H */
