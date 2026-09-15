/* irrg_unitbar.cpp -- unit action buttons (Phase 5b; made DYNAMIC in 5g)
 *
 * A bottom-centre bar of buttons for the currently focused (selected) unit.
 * The buttons are DYNAMIC: only the actions the focused unit can currently do
 * are shown (checked with unit_can_do_action(), or "has moves left" for the
 * free move/sentry/patrol commands). So a settler shows "Found City", a worker
 * shows "Irrigate"/"Mine"/..., an infantry shows "Pillage"/"Fortify"/..., etc.
 * Each button sends a gameplay action via FreeCiv's high-level request_* API.
 * Drawn into the 2D overlay (works in both 3D and 2D map views); clicks are
 * handled by irrg_unitbar_handle_click() (called before map clicks).
 */
#include "graphics.h"
#include "irrg_unitbar.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

/* The canvas draw functions are C symbols; canvas_g.h declares them with C++
 * linkage by default, so include it inside an extern "C" block to match. */
extern "C" {
#include "canvas_g.h"      /* canvas_put_text, canvas_put_rectangle, client_font */
}
#include "irrg_cxxside.h"  /* irrg_get_text_size (C++ linkage) */
#include "client_main.h"   /* client_state, C_S_RUNNING */
#include "unitlist.h"
#include "unit.h"          /* ORDER_LAST, unit fields, unit_can_do_action, unit_tile */
#include "unittype.h"      /* unit_type_get, utype_name_translation */
#include "control.h"       /* head_of_units_in_focus, request_unit_*, request_do_action */
#include "actions.h"       /* ACTION_* IDs */
#include "map.h"           /* tile_index */
#include "irrg_dialogs.h"  /* irrg_city_dialog_is_open */

/* How each button dispatches its action. */
enum ub_dispatch {
  UD_MOVE, UD_FORTIFY, UD_FOUND, UD_SENTRY, UD_PATROL, UD_DISBAND,
  UD_PILLAGE, UD_UPGRADE, UD_IRRIGATE, UD_MINE, UD_PLANT, UD_ROAD, UD_NUM
};

/* A candidate button. If special, availability = "focused unit has moves left"
 * (the free move/sentry/patrol commands); otherwise availability is
 * unit_can_do_action(focused unit, action). */
struct ub_candidate {
  const char *label;
  enum ub_dispatch dispatch;
  action_id action;
  bool special;
};
static const struct ub_candidate ub_cand[] = {
  { "Move",       UD_MOVE,     ACTION_NONE,         true  },
  { "Fortify",    UD_FORTIFY,  ACTION_FORTIFY,      false },
  { "Found City", UD_FOUND,    ACTION_FOUND_CITY,   false },
  { "Sentry",     UD_SENTRY,   ACTION_NONE,         true  },
  { "Patrol",     UD_PATROL,   ACTION_NONE,         true  },
  { "Pillage",    UD_PILLAGE,  ACTION_PILLAGE,      false },
  { "Upgrade",    UD_UPGRADE,  ACTION_UPGRADE_UNIT, false },
  { "Irrigate",   UD_IRRIGATE, ACTION_IRRIGATE,     false },
  { "Mine",       UD_MINE,     ACTION_MINE,         false },
  { "Plant",      UD_PLANT,    ACTION_PLANT,        false },
  { "Road",       UD_ROAD,     ACTION_ROAD,         false },
  { "Disband",    UD_DISBAND,  ACTION_DISBAND_UNIT, false },
};
#define UB_NCAND ((int)(sizeof(ub_cand) / sizeof(ub_cand[0])))
#define UB_MAX   16

/* The buttons actually shown for the focused unit (rebuilt on each draw). */
static const char *g_label[UB_MAX];
static int g_disp[UB_MAX];
static int g_nshown = 0;

/* Cached layout (from the most recent draw), used for hit-testing. */
static int g_W = 0;
static int g_btn_x[UB_MAX];
static int g_btn_w[UB_MAX];
static int g_btn_y = 0, g_btn_h = 30;

/* Mouse-driven feedback state: the cursor position (for hover highlighting)
 * and which button is currently held down (drawn "pressed"). */
static int g_mouse_x = -1, g_mouse_y = -1;
static int g_pressed = -1;

static bool ub_available(struct unit *fu, const struct ub_candidate *c)
{
  if (c->special)
    return fu->moves_left > 0;         /* free move/sentry/patrol commands */
  return unit_can_do_action(fu, c->action);
}

static void unitbar_build(struct unit *fu)
{
  g_nshown = 0;
  for (int i = 0; i < UB_NCAND && g_nshown < UB_MAX; ++i) {
    if (ub_available(fu, &ub_cand[i])) {
      g_label[g_nshown] = ub_cand[i].label;
      g_disp[g_nshown]  = (int)ub_cand[i].dispatch;
      ++g_nshown;
    }
  }
}

static void unitbar_layout(int W, int H)
{
  g_W = W;
  g_btn_h = 30;
  g_btn_y = H - g_btn_h - 10;
  int total = 0;
  for (int i = 0; i < g_nshown; ++i) {
    int tw = 0, th = 0;
    irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT, g_label[i]);
    g_btn_w[i] = tw + 18;
    total += g_btn_w[i];
  }
  total += 8 * (g_nshown > 0 ? g_nshown - 1 : 0);   /* inter-button gaps */
  int x = (W - total) / 2;
  for (int i = 0; i < g_nshown; ++i) {
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
   * edge, clear of the centered report body). */
  if (irrg_city_dialog_is_open()) return false;
  return head_of_units_in_focus() != nullptr;
}

static void unitbar_do_action(int disp)
{
  struct unit *fu = head_of_units_in_focus();
  if (!fu) return;
  switch (disp) {
  case UD_MOVE:    request_unit_goto(ORDER_LAST, ACTION_NONE, -1); break;
  case UD_FORTIFY: request_unit_fortify(fu); break;
  case UD_FOUND:   request_unit_build_city(fu); break;
  case UD_SENTRY:  request_unit_sentry(fu); break;
  case UD_PATROL:  request_unit_patrol(); break;
  case UD_DISBAND: request_unit_disband(fu); break;
  case UD_PILLAGE: request_unit_pillage(fu); break;
  case UD_UPGRADE: request_unit_upgrade(fu); break;
  case UD_IRRIGATE:
  case UD_MINE:
  case UD_PLANT:
  case UD_ROAD: {
    action_id a = (disp == UD_IRRIGATE) ? ACTION_IRRIGATE
                  : (disp == UD_MINE)   ? ACTION_MINE
                  : (disp == UD_PLANT)  ? ACTION_PLANT
                                         : ACTION_ROAD;
    request_do_action(a, fu->id, tile_index(unit_tile(fu)), 0, NULL);
    break;
  }
  default: break;
  }
}

/* Which button (0..g_nshown-1) contains (mx,my), or -1 if none. Uses the
 * cached layout from the most recent draw (g_W<=0 before the first draw). */
static int unitbar_hit(int mx, int my)
{
  if (g_W <= 0) return -1;
  if (my < g_btn_y || my >= g_btn_y + g_btn_h) return -1;
  for (int i = 0; i < g_nshown; ++i)
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
  struct unit *fu = head_of_units_in_focus();
  unitbar_build(fu);
  unitbar_layout(cv->width, cv->height);

  static struct color c_bg        = { 22, 24, 30 };
  static struct color c_bdr       = { 125, 135, 160 };
  static struct color c_tx        = { 226, 229, 239 };
  static struct color c_hbg       = { 30, 33, 42 };
  static struct color c_hy        = { 120, 215, 130 };
  static struct color c_hover     = { 46, 56, 82 };    /* lighter fill on hover  */
  static struct color c_hover_bdr = { 120, 205, 255 }; /* bright border on hover */
  static struct color c_pressed   = { 76, 118, 172 };  /* blue fill when pressed */
  static struct color c_press_bdr = { 195, 235, 255 }; /* bright border pressed  */

  /* Header strip above the buttons: which unit the bar controls (type name). */
  char hdr[96];
  std::snprintf(hdr, sizeof(hdr), "%s (unit %d) actions:",
                utype_name_translation(unit_type_get(fu)), fu->id);
  int hw = 0, hh = 0;
  irrg_get_text_size(&hw, &hh, FONT_REQTREE_TEXT, hdr);

  if (g_nshown <= 0) {
    /* No available actions (unit has no moves + nothing else to do). */
    int box_w = hw + 12;
    int box_x = (cv->width - box_w) / 2;
    canvas_put_rectangle(cv, &c_hbg, box_x, g_btn_y - hh - 6, box_w, hh + 4);
    canvas_put_text(cv, box_x + (box_w - hw) / 2, g_btn_y - hh - 4,
                    FONT_REQTREE_TEXT, &c_hy, hdr);
    canvas_put_text(cv, box_x, g_btn_y - 2, FONT_REQTREE_TEXT, &c_tx,
                    "(no available actions -- unit has no moves)");
    return;
  }

  int total = 0;
  for (int i = 0; i < g_nshown; ++i) total += g_btn_w[i];
  total += 8 * (g_nshown - 1);
  int box_w = (hw > total) ? hw : total;
  box_w += 12;
  int box_x = (cv->width - box_w) / 2;
  canvas_put_rectangle(cv, &c_hbg, box_x, g_btn_y - hh - 6, box_w, hh + 4);
  canvas_put_text(cv, box_x + (box_w - hw) / 2, g_btn_y - hh - 4,
                  FONT_REQTREE_TEXT, &c_hy, hdr);

  /* Buttons, with hover + press feedback. */
  for (int i = 0; i < g_nshown; ++i) {
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
    irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT, g_label[i]);
    canvas_put_text(cv, bx + (bw - tw) / 2, by + (bh - th) / 2,
                    FONT_REQTREE_TEXT, &c_tx, g_label[i]);
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
    fprintf(stderr, "[irrg] UBDBG: handle_click(%d,%d) visible=1 hit=%d (n=%d)\n",
            mx, my, hit, g_nshown);
  if (hit < 0) return false;
  unitbar_do_action(g_disp[hit]);
  return true;
}
