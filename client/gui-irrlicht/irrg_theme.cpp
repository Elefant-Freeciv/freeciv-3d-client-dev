/*
 * gui-irrlicht: Civ4 palette + beveled drawing helpers (implementation).
 * See irrg_theme.h.
 *
 * NOTE: irrg_canvas_put_*() take a NON-CONST `struct color *`, so each draw
 * function makes a local (non-const) copy of the palette and passes addresses
 * of its members.
 */
#include "irrg_theme.h"
#include "irrg_cxxside.h"   /* irrg_canvas_put_rectangle/text, irrg_get_text_size */
#include "canvas_g.h"       /* client_font (FONT_REQTREE_TEXT, FONT_CITY_NAME) */

struct irrg_civ_pal &irrg_civ(void)
{
  static struct irrg_civ_pal P = {
    {  34,  33,  30 },   /* bg       dark charcoal panel fill          */
    {  21,  20,  18 },   /* bg_dark darker inset                       */
    { 196, 170, 110 },   /* gold    border base (tan-gold)             */
    { 234, 210, 152 },   /* gold_hi bevel highlight (top/left)         */
    { 108,  90,  54 },   /* gold_lo bevel shadow (bottom/right)        */
    { 206, 181, 122 },   /* hdr_top header bar (light gold)            */
    { 149, 124,  74 },   /* hdr_bot header bar (dark gold)             */
    {  46,  35,  15 },   /* hdr_tx  header text (dark brown)           */
    { 231, 225, 207 },   /* text    cream body text                    */
    { 158, 151, 131 },   /* text_dim dim body text                     */
    {  97, 181,  71 },   /* green   research / OK accent               */
    {  45,  95,  37 },   /* green_lo darker green                      */
    {  48,  94, 150 },   /* blue    water / info accent                */
    { 180,  64,  44 },   /* red     warning accent                     */
    {  60,  56,  46 },   /* btn     button fill                        */
    {  95,  89,  68 },   /* btn_hi  button fill (hover)                */
  };
  return P;
}

/* A 2px gold beveled border: lighter along top/left, darker along bottom/right.
 * Gives the panels their "raised" 3-D frame without any textures. */
static void civ_border(struct canvas *cv, int x, int y, int w, int h)
{
  struct irrg_civ_pal P = irrg_civ();
  const int t = 2;
  irrg_canvas_put_rectangle(cv, &P.gold_hi, x, y, w, t);             /* top     */
  irrg_canvas_put_rectangle(cv, &P.gold_hi, x, y, t, h);             /* left    */
  irrg_canvas_put_rectangle(cv, &P.gold_lo, x, y + h - t, w, t);     /* bottom  */
  irrg_canvas_put_rectangle(cv, &P.gold_lo, x + w - t, y, t, h);     /* right   */
}

void irrg_civ_frame(struct canvas *cv, int x, int y, int w, int h)
{
  if (!cv || w <= 3 || h <= 3) return;
  civ_border(cv, x, y, w, h);
}

int irrg_civ_panel(struct canvas *cv, int x, int y, int w, int h, const char *title)
{
  if (!cv || w <= 4 || h <= 4) return y + 8;
  struct irrg_civ_pal P = irrg_civ();
  irrg_canvas_put_rectangle(cv, &P.bg, x, y, w, h);
  civ_border(cv, x, y, w, h);
  int content_top = y + 8;
  if (title && title[0]) {
    const int hh = 24;
    /* Gold header bar: a light-gold upper half + a darker lower half (a cheap
     * vertical gradient), then the title in dark brown. */
    irrg_canvas_put_rectangle(cv, &P.hdr_top, x + 2, y + 2, w - 4, (hh - 2) / 2);
    irrg_canvas_put_rectangle(cv, &P.hdr_bot, x + 2, y + 2 + (hh - 2) / 2, w - 4,
                              (hh - 2) / 2);
    int tw = 0, th = 0;
    irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT, title);
    irrg_canvas_put_text(cv, x + 10, y + 2 + ((hh - 2) - th) / 2,
                         FONT_REQTREE_TEXT, &P.hdr_tx, title);
    content_top = y + hh + 6;
  }
  return content_top;
}

void irrg_civ_button(struct canvas *cv, int x, int y, int w, int h,
                     const char *label, bool hovered, bool disabled)
{
  if (!cv || w <= 4 || h <= 4 || !label) return;
  struct irrg_civ_pal P = irrg_civ();
  struct color *fill = disabled ? &P.bg_dark : (hovered ? &P.btn_hi : &P.btn);
  irrg_canvas_put_rectangle(cv, fill, x, y, w, h);
  civ_border(cv, x, y, w, h);
  int tw = 0, th = 0;
  irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT, label);
  struct color *tc = disabled ? &P.text_dim : &P.text;
  irrg_canvas_put_text(cv, x + (w - tw) / 2, y + (h - th) / 2,
                       FONT_REQTREE_TEXT, tc, label);
}

void irrg_civ_bar(struct canvas *cv, int x, int y, int w, int h,
                  double frac, struct color *fg, const char *center)
{
  if (!cv || w <= 5 || h <= 5) return;
  struct irrg_civ_pal P = irrg_civ();
  irrg_canvas_put_rectangle(cv, &P.bg_dark, x, y, w, h);            /* inset     */
  double f = (frac < 0) ? 0 : ((frac > 1) ? 1 : frac);
  int fw = (int)((w - 4) * f);
  if (fw > 0 && fg)
    irrg_canvas_put_rectangle(cv, fg, x + 2, y + 2, fw, h - 4);     /* fill      */
  civ_border(cv, x, y, w, h);
  if (center && center[0]) {
    int tw = 0, th = 0;
    irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT, center);
    /* Cream over the dark part, dark over a mostly-full green fill, so the
     * centred label stays legible either way. */
    struct color *tc = (f > 0.14 && f < 0.86) ? &P.hdr_tx : &P.text;
    irrg_canvas_put_text(cv, x + (w - tw) / 2, y + (h - th) / 2,
                         FONT_REQTREE_TEXT, tc, center);
  }
}
