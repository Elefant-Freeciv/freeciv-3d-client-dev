/* irrg_unitbar.cpp -- unit action buttons (Phase 5b)
 *
 * A bottom-centre bar of buttons controlling the focused (selected) unit.
 * Buttons call FreeCiv's high-level request_unit_* API. Drawn into the 2D
 * overlay (works in both 3D and 2D map views); clicks are handled by
 * irrg_unitbar_handle_click() (called before map clicks in the event receiver).
 */
#include "graphics.h"
#include "irrg_unitbar.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

/* The canvas draw functions are C symbols (defined in the client's
 * gui_interface.c, wired to the irrlicht implementation via a callback table).
 * canvas_g.h declares them with C++ linkage by default, so include it inside an
 * extern "C" block to match (otherwise the mangled C++ name fails to link). */
extern "C" {
#include "canvas_g.h"      /* canvas_put_text, canvas_put_rectangle, client_font */
}
#include "irrg_cxxside.h"  /* irrg_get_text_size (C++ linkage) */
#include "client_main.h"   /* client_state, C_S_RUNNING */
#include "unitlist.h"      /* unit_list_size (unused guard) */
#include "unit.h"          /* ORDER_LAST, unit->id */
#include "control.h"       /* head_of_units_in_focus, request_unit_* */
#include "actions.h"       /* ACTION_NONE */
#include "irrg_dialogs.h"  /* irrg_city_dialog_is_open, irrg_report_or_help_open */

/* Available actions, in bar order. */
enum {
  ACT_MOVE, ACT_FORTIFY, ACT_FOUND, ACT_SENTRY, ACT_PATROL, ACT_DISBAND, ACT_NUM
};
static const char *btn_label[ACT_NUM] =
    { "Move", "Fortify", "Found City", "Sentry", "Patrol", "Disband" };

/* Cached layout (from the most recent draw), used for hit-testing. */
static int g_W = 0, g_H = 0;
static int g_btn_x[ACT_NUM];
static int g_btn_w[ACT_NUM];
static int g_btn_y = 0, g_btn_h = 30;

/* Mouse-driven feedback state: the cursor position (for hover highlighting)
 * and which button is currently held down (drawn "pressed"). */
static int g_mouse_x = -1, g_mouse_y = -1;
static int g_pressed = -1;

static void unitbar_layout(int W, int H)
{
  g_W = W;
  g_H = H;
  g_btn_h = 30;
  g_btn_y = H - g_btn_h - 10;
  int total = 0;
  for (int i = 0; i < ACT_NUM; ++i) {
    int tw = 0, th = 0;
    irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT, btn_label[i]);
    g_btn_w[i] = tw + 18;
    total += g_btn_w[i];
  }
  total += 8 * (ACT_NUM - 1);            /* inter-button gaps */
  int x = (W - total) / 2;
  for (int i = 0; i < ACT_NUM; ++i) {
    g_btn_x[i] = x;
    x += g_btn_w[i] + 8;
  }
}

bool irrg_unitbar_visible(void)
{
  if (client_state() != C_S_RUNNING) return false;
  /* The city control dialog is a large centered modal that owns the input
   * focus, so it takes the bar's slot. Reports/help panels can coexist with
   * the bar (the bar is drawn last, i.e. on top, and sits at the very bottom
   * edge, clear of the centered report body). Note: selecting a unit makes
   * the core open the units report via an idle callback -- the bar must stay
   * visible then, or it would hide exactly when the user most needs it. */
  if (irrg_city_dialog_is_open()) return false;
  return head_of_units_in_focus() != nullptr;
}

static void unitbar_do_action(int act)
{
  struct unit *fu = head_of_units_in_focus();
  if (!fu) { fprintf(stderr, "[irrg] UBDBG: do_action(%d) but no focused unit\n", act); return; }
  if (std::getenv("FC_IRR_UBDBG"))
    fprintf(stderr, "[irrg] UBDBG: do_action(%d) unit=%d\n", act, fu->id);
  switch (act) {
  case ACT_MOVE:    request_unit_goto(ORDER_LAST, ACTION_NONE, -1); break;
  case ACT_FORTIFY: request_unit_fortify(fu); break;
  case ACT_FOUND:   request_unit_build_city(fu); break;
  case ACT_SENTRY:  request_unit_sentry(fu); break;
  case ACT_PATROL:  request_unit_patrol(); break;
  case ACT_DISBAND: request_unit_disband(fu); break;
  }
}

/* Which button (0..ACT_NUM-1) contains (mx,my), or -1 if none. Uses the cached
 * layout from the most recent draw (g_W<=0 before the first draw). */
static int unitbar_hit(int mx, int my)
{
  if (g_W <= 0) return -1;
  if (my < g_btn_y || my >= g_btn_y + g_btn_h) return -1;
  for (int i = 0; i < ACT_NUM; ++i)
    if (mx >= g_btn_x[i] && mx < g_btn_x[i] + g_btn_w[i]) return i;
  return -1;
}

void irrg_unitbar_mouse_move(int mx, int my)
{
  g_mouse_x = mx;
  g_mouse_y = my;
}

void irrg_unitbar_mouse_down(int mx, int my)
{
  if (!irrg_unitbar_visible()) { g_pressed = -1; return; }
  g_pressed = unitbar_hit(mx, my);   /* -1 if the press was not on a button */
}

void irrg_unitbar_release(void)
{
  g_pressed = -1;   /* a pan consumed the press; don't keep the "pressed" look */
}

void irrg_unitbar_draw(struct canvas *cv)
{
  if (!cv) return;
  if (!irrg_unitbar_visible()) return;
  unitbar_layout(cv->width, cv->height);

  struct unit *fu = head_of_units_in_focus();

  static struct color c_bg        = { 22, 24, 30 };
  static struct color c_bdr       = { 125, 135, 160 };
  static struct color c_tx        = { 226, 229, 239 };
  static struct color c_hbg       = { 30, 33, 42 };
  static struct color c_hy        = { 120, 215, 130 };
  static struct color c_hover     = { 46, 56, 82 };    /* lighter fill on hover  */
  static struct color c_hover_bdr = { 120, 205, 255 }; /* bright border on hover */
  static struct color c_pressed   = { 76, 118, 172 };  /* blue fill when pressed */
  static struct color c_press_bdr = { 195, 235, 255 }; /* bright border pressed  */

  int total = 0;
  for (int i = 0; i < ACT_NUM; ++i) total += g_btn_w[i];
  total += 8 * (ACT_NUM - 1);
  int bx0 = (cv->width - total) / 2;

  /* Header strip above the buttons: which unit the bar controls. */
  char hdr[64];
  std::snprintf(hdr, sizeof(hdr), "Unit %d actions:", fu->id);
  int hw = 0, hh = 0;
  irrg_get_text_size(&hw, &hh, FONT_REQTREE_TEXT, hdr);
  canvas_put_rectangle(cv, &c_hbg, bx0 - 6, g_btn_y - hh - 6, total + 12, hh + 4);
  canvas_put_text(cv, bx0 - 2, g_btn_y - hh - 4, FONT_REQTREE_TEXT, &c_hy, hdr);

  /* Buttons, with hover + press feedback: the button under the cursor is
   * highlighted; a held-down button is drawn "pressed" (nudged down, blue). */
  for (int i = 0; i < ACT_NUM; ++i) {
    int bx = g_btn_x[i], bw = g_btn_w[i];
    bool hovered = (g_mouse_x >= bx && g_mouse_x < bx + bw
                    && g_mouse_y >= g_btn_y && g_mouse_y < g_btn_y + g_btn_h);
    bool pressed = (g_pressed == i);
    struct color fill, bdr;
    if (pressed)      { fill = c_pressed;  bdr = c_press_bdr; }
    else if (hovered) { fill = c_hover;    bdr = c_hover_bdr; }
    else              { fill = c_bg;       bdr = c_bdr; }
    int yoff = pressed ? 2 : 0;        /* sink a pressed button a couple of px */
    int by = g_btn_y + yoff;
    int bh = g_btn_h - yoff;
    canvas_put_rectangle(cv, &fill, bx, by, bw, bh);
    canvas_put_rectangle(cv, &bdr,  bx, by, bw, 2);
    canvas_put_rectangle(cv, &bdr,  bx, by + bh - 2, bw, 2);
    canvas_put_rectangle(cv, &bdr,  bx, by, 2, bh);
    canvas_put_rectangle(cv, &bdr,  bx + bw - 2, by, 2, bh);
    int tw = 0, th = 0;
    irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT, btn_label[i]);
    canvas_put_text(cv, bx + (bw - tw) / 2, by + (bh - th) / 2,
                    FONT_REQTREE_TEXT, &c_tx, btn_label[i]);
  }
}

bool irrg_unitbar_handle_click(int mx, int my)
{
  /* Runs on left-mouse release: clear the press visual regardless of outcome,
   * then dispatch the action if the release landed on a button. */
  g_pressed = -1;
  if (!irrg_unitbar_visible()) return false;
  int hit = unitbar_hit(mx, my);
  if (std::getenv("FC_IRR_UBDBG"))
    fprintf(stderr, "[irrg] UBDBG: handle_click(%d,%d) visible=1 hit=%d\n", mx, my, hit);
  if (hit < 0) return false;
  unitbar_do_action(hit);
  return true;
}
