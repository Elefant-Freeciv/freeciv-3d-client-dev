/*
 * gui-irrlicht: Phase 6 dialog surface (implementation).
 * See irrg_dialogs.h.
 */
#include "gui_main.h"
#include "irrg_dialogs.h"
#include "irrg_unitbar.h"  /* irrg_unitbar_draw (unit action buttons) */

#include "irrg_cxxside.h"  /* irrg_canvas_create / irrg_canvas_free */
#include "city.h"          /* struct city, city_name_get, city_population, ... */
#include "requirements.h"  /* universal_name_translation */
#include "canvas_g.h"      /* canvas_put_text, canvas_put_rectangle, client_font */
#include "client_main.h"   /* client_player() */
#include "player.h"        /* struct player { units, cities, name } */
#include "unitlist.h"      /* struct unit, unit_list_iterate, unit_list_size */
#include "unittype.h"      /* unit_name_translation(), utype_name_translation() */
#include "control.h"       /* head_of_units_in_focus() */
#include "irrg_map3d.h"    /* irrg_map3d_pick (FC_IRR_PICKGRID diagnostic) */
#include "unit.h"          /* unit fields, get_activity_text() */
#include "improvement.h"   /* improvement_iterate, struct impr_type */
#include "world_object.h"  /* wld.map (can_city_build_now needs the map) */
#include "map.h"           /* index_to_map_pos_x/y, tile_index, map_pos_to_tile */
#include "tile.h"          /* tile_terrain() */
#include "climap.h"        /* client_tile_get_known() */
#include "terrain.h"       /* terrain_type_terrain_class(), TC_OCEAN */
#include "citydlg_common.h"/* city_change_production, get_city_dialog_* */
#include <irrlicht.h>   /* irr::E_KEY_CODE for the production-menu key input */

#include <cstdio>
#include <cstring>
#include <string>

/* Static colors (struct color is just {r,g,b}; no alloc/free needed). */
static struct color c_white  = { 255, 255, 255 };
static struct color c_black  = {   0,   0,   0 };
static struct color c_yellow = { 255, 255,  64 };
static struct color c_bg     = {  32,  34,  40 };
static struct color c_border = { 120, 120, 130 };
static struct color c_msgbg  = {  20,  20,  24 };
static struct color c_title  = { 160, 220, 255 };
static struct color c_body   = { 210, 210, 210 };

/* ------------------------------------------------------------------ */
/* Message window (ring buffer of recent game messages).               */
/* ------------------------------------------------------------------ */
#define MSG_MAX   64
#define MSG_SHOW  5
#define MSG_LINE  22
#define MSG_W     470

static char g_msg[MSG_MAX][160];
static int  g_msg_count = 0;
static int  g_msg_next  = 0;

void irrg_message_append(const char *text)
{
  if (!text) return;
  /* Strip a single trailing newline so the line fits on one row. */
  std::snprintf(g_msg[g_msg_next], sizeof(g_msg[0]), "%s", text);
  char *nl = std::strchr(g_msg[g_msg_next], '\n');
  if (nl) *nl = '\0';
  g_msg_next = (g_msg_next + 1) % MSG_MAX;
  if (g_msg_count < MSG_MAX) g_msg_count++;
}

/* Clip a message to fit maxpx (append "..." if it must be cut). Keeps long
 * server-log lines from overflowing the message box into the rest of the
 * screen (e.g. over the bottom-centre unit action bar). */
static void irrg_msg_fit(char *dst, size_t n, const char *src, int maxpx)
{
  if (n == 0) return;
  int tw = 0, th = 0;
  irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT, src);
  if (tw <= maxpx) { std::snprintf(dst, n, "%s", src); return; }
  int ell = 0, eh = 0;
  irrg_get_text_size(&ell, &eh, FONT_REQTREE_TEXT, "...");
  int budget = maxpx - ell;
  if (budget < 8) budget = 8;
  char tmp[160];
  int lo = 0, hi = (int)std::strlen(src) - 1;
  while (lo < hi) {
    int mid = (lo + hi + 1) / 2;
    std::snprintf(tmp, sizeof(tmp), "%.*s", mid, src);
    irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT, tmp);
    if (tw <= budget) lo = mid; else hi = mid - 1;
  }
  std::snprintf(dst, n, "%.*s...", lo, src);
}

void irrg_draw_messages(struct canvas *cv)
{
  if (!cv || g_msg_count == 0) return;
  int show = (g_msg_count < MSG_SHOW) ? g_msg_count : MSG_SHOW;
  int x = 8;
  /* Lift the log clear of the bottom-centre unit action bar (which occupies
   * the bottom ~55px strip) so the two never collide when a unit is focused. */
  int bottom_margin = irrg_unitbar_visible() ? 70 : 8;
  int y = cv->height - show * MSG_LINE - bottom_margin;
  int textw = MSG_W - 10;
  for (int i = 0; i < show; i++) {
    int idx = (g_msg_next - show + i + 2 * MSG_MAX) % MSG_MAX;
    char line[160];
    irrg_msg_fit(line, sizeof(line), g_msg[idx], textw);
    canvas_put_rectangle(cv, &c_msgbg, x - 2, y + i * MSG_LINE - 2,
                         MSG_W, MSG_LINE - 1);
    canvas_put_text(cv, x, y + i * MSG_LINE, FONT_REQTREE_TEXT,
                    &c_white, line);
  }
}

/* ------------------------------------------------------------------ */
/* Basic city dialog.                                                 */
/* ------------------------------------------------------------------ */
static struct city *g_city_dlg = nullptr;

/* City-dialog layout (for mouse hit-testing): the panel rect, the build-list
 * area (top-left + width), and the close button. Filled in irrg_draw_city_dialog.
 * The game is meant to be playable with a mouse alone, so the build list +
 * close are click-driven, not just keyboard. */
static int g_cd_x = 0, g_cd_y = 0, g_cd_w = 0, g_cd_h = 0;
static int g_cd_list_x = 0, g_cd_list_y = 0, g_cd_list_w = 0;
static int g_cd_close_x = 0, g_cd_close_y = 0, g_cd_close_w = 0, g_cd_close_h = 0;

/* ---- City production menu (start-building control) ---- */
/* The list of things this city can build RIGHT NOW (improvements + units),
 * built when the dialog opens. The user navigates it with the arrow keys and
 * starts production on the selected item with Enter (city_change_production). */
#define CITY_PROD_MAX   160
#define CITY_PROD_SHOW  12      /* rows of the list shown at once */
static struct universal g_cp_list[CITY_PROD_MAX];
static char g_cp_name[CITY_PROD_MAX][48];
static int  g_cp_n      = 0;    /* number of buildable items */
static int  g_cp_sel    = 0;    /* selected index */
static int  g_cp_scroll = 0;    /* first visible index */

/* Refresh the buildable list for pcity and point the selection at the city's
 * current production (so "Enter" re-affirms it; arrows change it). */
static void irrg_city_build_prod_list(struct city *pcity)
{
  g_cp_n = 0;
  improvement_iterate(pimprove) {
    struct universal u;
    u.kind = VUT_IMPROVEMENT; u.value.building = pimprove;
    if (g_cp_n < CITY_PROD_MAX
        && can_city_build_now(&wld.map, pcity, &u, RPT_CERTAIN)) {
      g_cp_list[g_cp_n] = u;
      g_cp_name[g_cp_n][0] = '\0';
      universal_name_translation(&u, g_cp_name[g_cp_n],
                                 sizeof g_cp_name[g_cp_n]);
      g_cp_n++;
    }
  } improvement_iterate_end;
  unit_type_iterate(punittype) {
    struct universal u;
    u.kind = VUT_UTYPE; u.value.utype = punittype;
    if (g_cp_n < CITY_PROD_MAX
        && can_city_build_now(&wld.map, pcity, &u, RPT_CERTAIN)) {
      g_cp_list[g_cp_n] = u;
      g_cp_name[g_cp_n][0] = '\0';
      universal_name_translation(&u, g_cp_name[g_cp_n],
                                 sizeof g_cp_name[g_cp_n]);
      g_cp_n++;
    }
  } unit_type_iterate_end;
  g_cp_sel = 0; g_cp_scroll = 0;
  for (int i = 0; i < g_cp_n; ++i)
    if (are_universals_equal(&g_cp_list[i], &pcity->production)) { g_cp_sel = i; break; }
  g_cp_scroll = CLIP(0, g_cp_sel - CITY_PROD_SHOW / 2, g_cp_n - CITY_PROD_SHOW);
}

void irrg_city_dialog_open(struct city *pcity)
{
  g_city_dlg = pcity;
  if (pcity)
    irrg_city_build_prod_list(pcity);
}
void irrg_city_dialog_close(void) { g_city_dlg = nullptr; }
bool   irrg_city_dialog_is_open(void) { return g_city_dlg != nullptr; }
bool   irrg_city_dialog_open_for(struct city *pcity) { return g_city_dlg == pcity; }

/* Handle a key press for the city production menu. Returns TRUE if the key was
 * consumed (so the caller should not also pan the map / fire shortcuts).
 * Up/Down move the selection, PgUp/PgDn scroll a page, Enter/Space start
 * production on the selected item (city_change_production -> the server). */
bool irrg_city_dialog_handle_key(int key, bool press)
{
  if (!g_city_dlg || g_cp_n == 0) return false;
  if (!press) return false;
  auto clamp_scroll = [&] {
    if (g_cp_sel < g_cp_scroll) g_cp_scroll = g_cp_sel;
    if (g_cp_sel >= g_cp_scroll + CITY_PROD_SHOW)
      g_cp_scroll = g_cp_sel - CITY_PROD_SHOW + 1;
    if (g_cp_scroll < 0) g_cp_scroll = 0;
  };
  switch (key) {
  case irr::KEY_UP:    if (g_cp_sel > 0) { g_cp_sel--; clamp_scroll(); } return true;
  case irr::KEY_DOWN:
    if (g_cp_sel < g_cp_n - 1) { g_cp_sel++; clamp_scroll(); } return true;
  case irr::KEY_RETURN:
  case irr::KEY_SPACE:
    city_change_production(g_city_dlg, &g_cp_list[g_cp_sel]);
    return true;
  default:
    return false;
  }
}

/* Headless test: select buildable #index and start production on it. Returns
 * TRUE if a production-start was sent (so a test can later check the city's
 * production actually changed). */
bool irrg_city_dialog_test_build(int index)
{
  if (!g_city_dlg || index < 0 || index >= g_cp_n) return false;
  g_cp_sel = index;
  city_change_production(g_city_dlg, &g_cp_list[index]);
  return true;
}

/* Mouse input for the city dialog: a click on a build-list row starts building
 * that item (the same city_change_production the keyboard Enter sends); a click
 * on the close button dismisses it. Any click inside the panel is consumed so it
 * does NOT also select/move a unit on the map behind the dialog. Returns TRUE if
 * the click was inside the dialog (so the caller must not hit-test the map). */
bool irrg_city_dialog_click(int x, int y, bool left)
{
  if (!g_city_dlg || g_cd_w <= 0) return false;
  if (x < g_cd_x || x >= g_cd_x + g_cd_w || y < g_cd_y || y >= g_cd_y + g_cd_h)
    return false;                        /* outside the panel -> let it hit the map */
  if (left) {
    /* Close button? */
    if (x >= g_cd_close_x && x < g_cd_close_x + g_cd_close_w
        && y >= g_cd_close_y && y < g_cd_close_y + g_cd_close_h) {
      irrg_city_dialog_close();
      return true;
    }
    /* A build-list row? -> select + build it. */
    if (g_cp_n > 0 && x >= g_cd_list_x && x < g_cd_list_x + g_cd_list_w
        && y >= g_cd_list_y && y < g_cd_list_y + CITY_PROD_SHOW * 20) {
      int i = (y - g_cd_list_y) / 20;
      int idx = g_cp_scroll + i;
      if (i >= 0 && i < CITY_PROD_SHOW && idx >= 0 && idx < g_cp_n) {
        g_cp_sel = idx;
        city_change_production(g_city_dlg, &g_cp_list[idx]);
        if (getenv("FC_IRR_UBDBG")) {
          fprintf(stderr, "[irrg] city dialog click -> build '%s' (idx %d)\n",
                  g_cp_name[idx], idx);
          fflush(stderr);
        }
      }
    }
  }
  return true;                            /* consumed (inside the dialog) */
}

void irrg_draw_city_dialog(struct canvas *cv)
{
  if (!cv || !g_city_dlg) return;
  struct city *c = g_city_dlg;

  const int list_h = CITY_PROD_SHOW * 20;
  const int bw = 470, bh = 210 + list_h;
  const int bx = cv->width - bw - 14, by = 14;
  g_cd_x = bx; g_cd_y = by; g_cd_w = bw; g_cd_h = bh;
  g_cd_close_x = bx + bw - 36; g_cd_close_y = by + 6;
  g_cd_close_w = 30; g_cd_close_h = 22;
  static struct color c_sel   = {  70,  90, 150 };  /* selected row bg   */
  static struct color c_cur   = { 120, 210, 120 };  /* current prod mark */
  static struct color c_hdr   = { 170, 190, 230 };  /* section header    */

  /* Panel + border. */
  canvas_put_rectangle(cv, &c_bg,     bx,     by,     bw, bh);
  canvas_put_rectangle(cv, &c_border, bx,     by,     bw, 2);
  canvas_put_rectangle(cv, &c_border, bx,     by + bh - 2, bw, 2);
  canvas_put_rectangle(cv, &c_border, bx,     by,     2,  bh);
  canvas_put_rectangle(cv, &c_border, bx + bw - 2, by, 2,  bh);

  /* Close button (top-right): a mouse affordance for dismissing the dialog. */
  {
    static struct color c_clb = { 66, 42, 46 };
    static struct color c_clt = { 245, 140, 150 };
    canvas_put_rectangle(cv, &c_clb, g_cd_close_x, g_cd_close_y,
                         g_cd_close_w, g_cd_close_h);
    int cw = 0, chh = 0;
    irrg_get_text_size(&cw, &chh, FONT_REQTREE_TEXT, "X");
    canvas_put_text(cv, g_cd_close_x + (g_cd_close_w - cw) / 2,
                    g_cd_close_y + (g_cd_close_h - chh) / 2,
                    FONT_REQTREE_TEXT, &c_clt, "X");
  }

  char line[180], out[64];
  int ty = by + 12;

  canvas_put_text(cv, bx + 12, ty, FONT_CITY_NAME, &c_yellow,
                  city_name_get(c));
  ty += 42;   /* city name renders at 2x (38px tall) */

  std::snprintf(line, sizeof(line), "Population: %d   Size: %d",
                city_population(c), (int)city_size_get(c));
  canvas_put_text(cv, bx + 12, ty, FONT_REQTREE_TEXT, &c_white, line);
  ty += 22;

  /* get_city_dialog_output_text() returns a multi-line breakdown (total +
   * sources); keep only the first (total) line of each so the three fit on a
   * single row without the source lines bleeding in. */
  get_city_dialog_output_text(c, O_FOOD, out, sizeof(out));
  char foodout[40]; std::snprintf(foodout, sizeof(foodout), "%s", out);
  if (char *nl = std::strchr(foodout, '\n')) *nl = '\0';
  get_city_dialog_output_text(c, O_SHIELD, out, sizeof(out));
  char shdout[40]; std::snprintf(shdout, sizeof(shdout), "%s", out);
  if (char *nl = std::strchr(shdout, '\n')) *nl = '\0';
  get_city_dialog_output_text(c, O_SCIENCE, out, sizeof(out));
  char sciout[40]; std::snprintf(sciout, sizeof(sciout), "%s", out);
  if (char *nl = std::strchr(sciout, '\n')) *nl = '\0';
  std::snprintf(line, sizeof(line), "Food: %s   Shields: %s   Science: %s",
                foodout, shdout, sciout);
  canvas_put_text(cv, bx + 12, ty, FONT_REQTREE_TEXT, &c_body, line);
  ty += 24;

  /* Current production line. */
  char prodfull[160];
  get_city_dialog_production_full(prodfull, sizeof(prodfull), &c->production, c);
  canvas_put_text(cv, bx + 12, ty, FONT_REQTREE_TEXT, &c_cur,
                  (std::string("Building: ") + prodfull).c_str());
  ty += 24;

  /* Production menu header. */
  canvas_put_rectangle(cv, &c_border, bx, ty, bw, 1);
  ty += 6;
  char hdline[96];
  std::snprintf(hdline, sizeof(hdline),
                "Start building (click a row): %d available", g_cp_n);
  canvas_put_text(cv, bx + 12, ty, FONT_REQTREE_TEXT, &c_hdr, hdline);
  ty += 22;

  /* Buildable list (scrollable). */
  const int list_x = bx + 12;
  const int list_w = bw - 24;
  g_cd_list_x = list_x; g_cd_list_y = ty; g_cd_list_w = list_w;   /* for clicks */
  for (int i = 0; i < CITY_PROD_SHOW; ++i) {
    int idx = g_cp_scroll + i;
    if (idx >= g_cp_n) break;
    int ry = ty + i * 20;
    if (idx == g_cp_sel)
      canvas_put_rectangle(cv, &c_sel, list_x, ry, list_w, 18);
    struct color *tc = (idx == g_cp_sel) ? &c_yellow : &c_body;
    bool is_cur = are_universals_equal(&g_cp_list[idx], &c->production);
    int tx = list_x + 4;
    if (is_cur) {                      /* mark the current production with >> */
      canvas_put_text(cv, list_x + 4, ry + 1, FONT_REQTREE_TEXT, &c_cur, ">>");
      tx = list_x + 26;
    }
    canvas_put_text(cv, tx, ry + 1, FONT_REQTREE_TEXT, tc, g_cp_name[idx]);
  }
  ty += list_h + 6;

  /* Footer (kept short so it fits the 470px-wide panel; mouse clicks work, the
   * arrow keys are the fallback -- Irrlicht 1.8.5 has no PgUp/PgDn). */
  canvas_put_text(cv, bx + 12, by + bh - 22, FONT_REQTREE_TEXT,
                  &c_yellow, "[click a row:build]   [X:close]");
}

/* ==================================================================
 * Reports + Help dialogs (Phase 6).
 *
 * Read the client's world state (client_player()) directly and draw a canvas
 * panel. The report vtable functions open the relevant report; the city dialog
 * and reports/help share the ESC-to-close handling in irrg_interact.
 * ================================================================== */
enum { REP_UNITS = 0, REP_SCIENCE = 1, REP_ECONOMY = 2 };
static int g_report_open = 0;
static int g_report_kind = REP_UNITS;
static int g_help_open   = 0;
static int g_log_open    = 0;   /* the "Messages" submenu (game-log panel) */

void irrg_reports_open(int kind)  { g_report_open = 1; g_report_kind = kind; g_help_open = 0; g_log_open = 0; }
void irrg_reports_close(void)     { g_report_open = 0; }
void irrg_help_open(void)         { g_help_open = 1; g_report_open = 0; g_log_open = 0; }
void irrg_help_close(void)        { g_help_open = 0; }
void irrg_log_open(void)          { g_log_open = 1; g_report_open = 0; g_help_open = 0; }
void irrg_log_close(void)         { g_log_open = 0; }
bool irrg_log_is_open(void)       { return g_log_open != 0; }

/* Close the topmost report/help/messages dialog. Returns 1 if one was closed
 * (so the caller can fall through to the city dialog). */
int irrg_close_topmost_report_or_help(void)
{
  if (g_report_open) { g_report_open = 0; return 1; }
  if (g_help_open)   { g_help_open = 0;  return 1; }
  if (g_log_open)    { g_log_open = 0;   return 1; }
  return 0;
}

bool irrg_report_or_help_open(void) { return (g_report_open != 0 || g_help_open != 0 || g_log_open != 0); }

/* Mouse close for the report/help/log panels: while one is open, ANY left click
 * dismisses it (the mouse equivalent of ESC). A small "Close" button is drawn on
 * the panel as a visible affordance for WHERE to click. Returns TRUE if a panel
 * was open and the click was consumed (the caller must not hit-test the map). */
bool irrg_panel_click(int x, int y)
{
  (void)x; (void)y;
  if (irrg_report_or_help_open()) {
    irrg_close_topmost_report_or_help();
    return true;
  }
  return false;
}

static const char *irrg_rep_title(int kind)
{
  switch (kind) {
  case REP_UNITS:   return "Units Report";
  case REP_SCIENCE: return "Science Report";
  case REP_ECONOMY: return "Economy Report";
  }
  return "Report";
}

static int irrg_rep_build(char out[][140], int maxlines, int kind)
{
  struct player *me = client_player();
  int n = 0;
  if (!me) { std::snprintf(out[n++], 140, "(not connected)"); return n; }
  if (n < maxlines) std::snprintf(out[n++], 140, "Player: %s", me->name);

  if (kind == REP_UNITS) {
    int cnt = 0;
    if (me->units) {
      struct unit *punit;
      unit_list_iterate(me->units, punit) {
        cnt++;
        if (n < maxlines - 1)
          std::snprintf(out[n++], 140, "  %-20s  #%d",
                        unit_name_translation(punit), punit->id);
      } unit_list_iterate_end;
    }
    if (n < maxlines) std::snprintf(out[n++], 140, "(%d unit(s) total)", cnt);
  } else if (kind == REP_ECONOMY) {
    if (me->cities) {
      struct city *pcity;
      city_list_iterate(me->cities, pcity) {
        if (n < maxlines - 1)
          std::snprintf(out[n++], 140, "  %-20s  population %d",
                        city_name_get(pcity), city_population(pcity));
      } city_list_iterate_end;
    }
    if (n < maxlines) std::snprintf(out[n++], 140, "(%d city(ies))",
                                    city_list_size(me->cities));
  } else { /* REP_SCIENCE */
    if (n < maxlines) std::snprintf(out[n++], 140,
        "Current research: see your capital's city dialog.");
  }
  return n;
}

static void irrg_draw_panel_lines(struct canvas *cv, const char *title,
                                  char lines[][140], int nlines)
{
  const int pw = 560, line_h = 20, head_h = 46;
  const int shown = nlines < 22 ? nlines : 22;
  const int ph = head_h + shown * line_h + 24;
  int px = (cv->width - pw) / 2, py = 110;
  canvas_put_rectangle(cv, &c_bg,     px, py,            pw, ph);
  canvas_put_rectangle(cv, &c_border, px, py,            pw, 2);
  canvas_put_rectangle(cv, &c_border, px, py + ph - 2,   pw, 2);
  canvas_put_rectangle(cv, &c_border, px, py,            2,  ph);
  canvas_put_rectangle(cv, &c_border, px + pw - 2, py,   2,  ph);
  canvas_put_text(cv, px + 14, py + 8, FONT_CITY_NAME, &c_title, title);
  /* Close button (top-right): a visible affordance -- any click dismisses the
   * panel (irrg_panel_click), so it can be closed with a mouse alone. */
  {
    static struct color c_clb = { 66, 42, 46 };
    static struct color c_clt = { 245, 140, 150 };
    const int cbw = 56, cbh = 22;
    int cbx = px + pw - cbw - 6, cby = py + 8;
    canvas_put_rectangle(cv, &c_clb, cbx, cby, cbw, cbh);
    int cw = 0, chh = 0;
    irrg_get_text_size(&cw, &chh, FONT_REQTREE_TEXT, "Close");
    canvas_put_text(cv, cbx + (cbw - cw) / 2, cby + (cbh - chh) / 2,
                    FONT_REQTREE_TEXT, &c_clt, "Close");
  }
  int ty = py + head_h;
  for (int i = 0; i < shown; ++i) {
    canvas_put_text(cv, px + 14, ty, FONT_REQTREE_TEXT, &c_body, lines[i]);
    ty += line_h;
  }
  canvas_put_text(cv, px + 14, py + ph - 20, FONT_REQTREE_TEXT, &c_yellow,
                  "[ESC / click to close]");
}

static void irrg_draw_report(struct canvas *cv)
{
  char lines[40][140];
  int n = irrg_rep_build(lines, 40, g_report_kind);
  irrg_draw_panel_lines(cv, irrg_rep_title(g_report_kind), lines, n);
}

static void irrg_draw_help(struct canvas *cv)
{
  static const char *help[] = {
    "FreeCiv - Irrlicht 3D client",
    "",
    "Mouse:  drag = pan map,  wheel = zoom",
    "        right-click = select unit, then click to move (2D)",
    "Keys:   arrows / WASD = pan,  +/- = zoom",
    "        ESC = close dialog / cancel move",
    "        H = help   R = units report",
    "        O = economy report   S = science report",
    "",
    "Click your capital to open the city dialog.",
    "3D terrain view: run with FC_IRR_MAP3D=1",
  };
  char lines[24][140];
  int n = 0;
  for (const char *s : help) if (n < 24) std::snprintf(lines[n++], 140, "%s", s);
  irrg_draw_panel_lines(cv, "Help", lines, n);
}

/* "Messages" submenu: the recent game log, shown as a centered panel (like the
 * reports) when opened. This replaced the always-on bottom-left message window,
 * so the log no longer sits in the way of the map / the unit dialog. */
static void irrg_draw_messages_panel(struct canvas *cv)
{
  char lines[40][140];
  int n = 0;
  int show = (g_msg_count < 22) ? g_msg_count : 22;
  for (int i = 0; i < show; i++) {
    int idx = (g_msg_next - show + i + 2 * MSG_MAX) % MSG_MAX;
    char fit[140];
    irrg_msg_fit(fit, sizeof(fit), g_msg[idx], 520);
    std::snprintf(lines[n++], 140, "%s", fit);
  }
  if (n == 0)
    std::snprintf(lines[n++], 140, "(no messages yet)");
  irrg_draw_panel_lines(cv, "Messages", lines, n);
}

/* Selected-unit info panel (bottom-left corner): describes the focused
 * (selected) unit. Shown whenever a unit is focused and no modal owns that
 * slot, in both the 3D and 2D map views. This is where the old message window
 * used to sit (the log now lives in the "Messages" submenu). */
/* The unit dialog's current rect, for click hit-testing (click it to centre
 * the 3D view on that unit). */
static int g_udlg_x = 0, g_udlg_y = 0, g_udlg_w = 0, g_udlg_h = 0;
bool irrg_unit_dialog_hit(int mx, int my)
{
  return g_udlg_w > 0 && mx >= g_udlg_x && mx < g_udlg_x + g_udlg_w &&
         my >= g_udlg_y && my < g_udlg_y + g_udlg_h;
}

static void irrg_draw_unit_dialog(struct canvas *cv)
{
  g_udlg_w = 0;   /* not drawn (until the rect is computed below) */
  if (!cv || client_state() != C_S_RUNNING) return;
  if (irrg_city_dialog_is_open()) return;   /* the city dialog owns that slot */
  struct unit *fu = head_of_units_in_focus();
  if (!fu) return;

  if (std::getenv("FC_IRR_UBDBG")) {
    static int dcnt = 0;
    if ((dcnt++ % 40) == 0)
      std::fprintf(stderr, "[irrg] UNITDLG focus id=%d moves=%d\n", fu->id, fu->moves_left);
    std::fflush(stderr);
  }

  static struct color c_udbg  = { 22, 25, 33 };
  static struct color c_udbrd = { 110, 150, 205 };
  static struct color c_utt   = { 140, 215, 255 };
  static struct color c_utx   = { 225, 228, 238 };

  const struct unit_type *ut = unit_type_get(fu);
  const char *uname = (ut && utype_name_translation(ut))
                         ? utype_name_translation(ut) : "Unit";
  const char *owner = fu->owner ? player_name(fu->owner) : "";

  char l_hp[120], l_mv[120], l_act[120], l_own[120], l_extra[120];
  std::snprintf(l_hp,  sizeof(l_hp),  "HP: %d / %d", fu->hp, ut ? ut->hp : 100);
  std::snprintf(l_mv,  sizeof(l_mv),  "Moves: %d", fu->moves_left);
  std::snprintf(l_act, sizeof(l_act), "Activity: %s", get_activity_text(fu->activity));
  std::snprintf(l_own, sizeof(l_own), "Owner: %s", owner);
  if (fu->veteran > 0)
    std::snprintf(l_extra, sizeof(l_extra), "Veteran (level %d)", fu->veteran);
  else if (fu->goto_tile)
    std::snprintf(l_extra, sizeof(l_extra), "Moving to (%d,%d)",
                  index_to_map_pos_x(tile_index(fu->goto_tile)),
                  index_to_map_pos_y(tile_index(fu->goto_tile)));
  else
    l_extra[0] = '\0';

  const int line = 20, pad = 8, W = 272;
  int title_w = 0, title_h = 0;
  irrg_get_text_size(&title_w, &title_h, FONT_CITY_NAME, uname);  /* 2x font */
  title_h += 8;   /* breathing room below the (taller) title glyphs */
  int datalines = 4 + (l_extra[0] ? 1 : 0);   /* 4 lines + optional extra */
  int H = pad + title_h + datalines * line + pad;   /* top + title + data + bottom */
  int x = 8, y = cv->height - H - 8;
  g_udlg_x = x; g_udlg_y = y; g_udlg_w = W; g_udlg_h = H;   /* for click hit-test */

  canvas_put_rectangle(cv, &c_udbg,  x, y, W, H);
  canvas_put_rectangle(cv, &c_udbrd, x, y, W, 2);
  canvas_put_rectangle(cv, &c_udbrd, x, y + H - 2, W, 2);
  canvas_put_rectangle(cv, &c_udbrd, x, y, 2, H);
  canvas_put_rectangle(cv, &c_udbrd, x + W - 2, y, 2, H);

  /* title (city-name font, a bit taller), then the data rows below it */
  int ty = y + pad;
  canvas_put_text(cv, x + pad, ty, FONT_CITY_NAME, &c_utt, uname);
  ty += title_h;
  canvas_put_text(cv, x + pad, ty, FONT_REQTREE_TEXT, &c_utx, l_hp);  ty += line;
  canvas_put_text(cv, x + pad, ty, FONT_REQTREE_TEXT, &c_utx, l_mv);  ty += line;
  canvas_put_text(cv, x + pad, ty, FONT_REQTREE_TEXT, &c_utx, l_act); ty += line;
  canvas_put_text(cv, x + pad, ty, FONT_REQTREE_TEXT, &c_utx, l_own); ty += line;
  if (l_extra[0])
    canvas_put_text(cv, x + pad, ty, FONT_REQTREE_TEXT, &c_utx, l_extra);
}

/* ------------------------------------------------------------------ */
/* End Turn button (top-right). A persistent, always-available command in the
 * in-game view -- unlike the unit action bar, it does not need a focused unit.
 * Clicking it ends the current player's turn (same path as the in-game menu). */
static int g_et_x = 0, g_et_y = 0, g_et_w = 0, g_et_h = 28;
static int g_et_mx = -1, g_et_my = -1;

void irrg_draw_endturn_button(struct canvas *cv)
{
  if (!cv) return;
  if (client_state() != C_S_RUNNING) return;
  if (irrg_city_dialog_is_open()) return;    /* hidden while a modal is up */
  static struct color c_etbg     = { 22, 24, 30 };
  static struct color c_etbdr    = { 125, 135, 160 };
  static struct color c_ettx     = { 226, 229, 239 };
  static struct color c_ethover  = { 46, 56, 82 };    /* lighter fill on hover */
  static struct color c_ethoverb = { 120, 205, 255 }; /* bright border on hover */
  const char *label = "End Turn";
  int tw = 0, th = 0;
  irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT, label);
  g_et_w = tw + 20;
  g_et_h = th + 12;
  g_et_x = cv->width - g_et_w - 10;
  g_et_y = 8;
  bool hovered = (g_et_mx >= g_et_x && g_et_mx < g_et_x + g_et_w
                  && g_et_my >= g_et_y && g_et_my < g_et_y + g_et_h);
  struct color fill = hovered ? c_ethover : c_etbg;
  struct color bdr  = hovered ? c_ethoverb : c_etbdr;
  canvas_put_rectangle(cv, &fill, g_et_x, g_et_y, g_et_w, g_et_h);
  canvas_put_rectangle(cv, &bdr,  g_et_x, g_et_y, g_et_w, 2);
  canvas_put_rectangle(cv, &bdr,  g_et_x, g_et_y + g_et_h - 2, g_et_w, 2);
  canvas_put_rectangle(cv, &bdr,  g_et_x, g_et_y, 2, g_et_h);
  canvas_put_rectangle(cv, &bdr,  g_et_x + g_et_w - 2, g_et_y, 2, g_et_h);
  canvas_put_text(cv, g_et_x + (g_et_w - tw) / 2, g_et_y + (g_et_h - th) / 2,
                  FONT_REQTREE_TEXT, &c_ettx, label);
}

bool irrg_endturn_button_hit(int mx, int my)
{
  if (client_state() != C_S_RUNNING) return false;
  if (irrg_city_dialog_is_open()) return false;
  if (g_et_x <= 0) return false;
  return (mx >= g_et_x && mx < g_et_x + g_et_w
          && my >= g_et_y && my < g_et_y + g_et_h);
}

void irrg_endturn_mouse_move(int mx, int my)
{
  g_et_mx = mx;
  g_et_my = my;
}

/* ------------------------------------------------------------------ */
/* In-game persistent "Menu" button (top-right, to the LEFT of End Turn).
 * Opens the in-game menu -- the single GUI entry point for every action
 * (reports, help, settings, fog, end turn, quit). Because it is always on
 * screen, the whole game is operable with a mouse alone (no ESC needed). */
static int g_mb_x = 0, g_mb_y = 0, g_mb_w = 0, g_mb_h = 28;
static int g_mb_mx = -1, g_mb_my = -1;

void irrg_draw_menu_button(struct canvas *cv)
{
  g_mb_w = 0;
  if (!cv) return;
  if (client_state() != C_S_RUNNING) return;
  if (irrg_city_dialog_is_open()) return;     /* hidden while a modal is up */
  static struct color c_mb    = { 22, 24, 30 };
  static struct color c_mbd   = { 125, 135, 160 };
  static struct color c_mbt   = { 226, 229, 239 };
  static struct color c_mbh   = { 46, 56, 82 };      /* lighter fill on hover */
  static struct color c_mhbrd = { 120, 205, 255 };   /* bright border on hover */
  const char *label = "Menu";
  int tw = 0, th = 0;
  irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT, label);
  g_mb_w = tw + 20;
  g_mb_h = th + 12;
  /* Sit to the LEFT of the End Turn button (drawn just before us in
   * irrg_draw_dialogs). Fall back to the right edge if it isn't available. */
  g_mb_y = g_et_y;
  g_mb_x = (g_et_x > 0) ? (g_et_x - 8 - g_mb_w) : (cv->width - g_mb_w - 10);
  bool hovered = (g_mb_mx >= g_mb_x && g_mb_mx < g_mb_x + g_mb_w
                  && g_mb_my >= g_mb_y && g_mb_my < g_mb_y + g_mb_h);
  struct color fill = hovered ? c_mbh : c_mb;
  struct color bdr  = hovered ? c_mhbrd : c_mbd;
  canvas_put_rectangle(cv, &fill, g_mb_x, g_mb_y, g_mb_w, g_mb_h);
  canvas_put_rectangle(cv, &bdr,  g_mb_x, g_mb_y, g_mb_w, 2);
  canvas_put_rectangle(cv, &bdr,  g_mb_x, g_mb_y + g_mb_h - 2, g_mb_w, 2);
  canvas_put_rectangle(cv, &bdr,  g_mb_x, g_mb_y, 2, g_mb_h);
  canvas_put_rectangle(cv, &bdr,  g_mb_x + g_mb_w - 2, g_mb_y, 2, g_mb_h);
  canvas_put_text(cv, g_mb_x + (g_mb_w - tw) / 2, g_mb_y + (g_mb_h - th) / 2,
                  FONT_REQTREE_TEXT, &c_mbt, label);
}

bool irrg_menu_button_hit(int mx, int my)
{
  if (client_state() != C_S_RUNNING) return false;
  if (irrg_city_dialog_is_open()) return false;
  if (g_mb_w <= 0) return false;
  return (mx >= g_mb_x && mx < g_mb_x + g_mb_w
          && my >= g_mb_y && my < g_mb_y + g_mb_h);
}

void irrg_menu_button_mouse_move(int mx, int my)
{
  g_mb_mx = mx;
  g_mb_my = my;
}

/* ---- Minimap (3D): a small colour map of the whole explored map + a viewport
 * rectangle showing where the 3D camera is looking. Click it to jump there. ---- */
static int g_mm_x = 0, g_mm_y = 0, g_mm_s = 0;   /* box (set during draw) */

static void mm_tile_color(const struct tile *pt, struct color *c)
{
  if (!pt || client_tile_get_known(pt) == TILE_UNKNOWN || !tile_terrain(pt)) {
    c->r = 20; c->g = 20; c->b = 24;            /* fog / not explored */
    return;
  }
  if (terrain_type_terrain_class(tile_terrain(pt)) == TC_OCEAN) {
    c->r = 26; c->g = 74; c->b = 165;           /* water */
  } else {
    c->r = 62; c->g = 122; c->b = 52;           /* land */
  }
}

void irrg_draw_minimap(struct canvas *cv)
{
  if (!cv || !cv->pixels) return;
  if (!irrg_map3d_is_built()) return;   /* 3D only (the viewport is the 3D cam) */
  const int MM = 150;
  const int mw = wld.map.xsize, mh = wld.map.ysize;
  if (mw <= 0 || mh <= 0) return;
  g_mm_x = cv->width - MM - 10;
  g_mm_y = cv->height - MM - 10;
  g_mm_s = MM;
  irr::video::SColor *cvpx = (irr::video::SColor *)cv->pixels;
  for (int y = 0; y < MM; ++y) {
    int ty = (y * mh) / MM;
    for (int x = 0; x < MM; ++x) {
      int tx = (x * mw) / MM;
      struct tile *pt = map_pos_to_tile(&wld.map, tx, ty);
      struct color col;
      mm_tile_color(pt, &col);
      cvpx[(g_mm_y + y) * cv->width + (g_mm_x + x)] =
          irr::video::SColor(255, col.r, col.g, col.b);
    }
  }
  /* Viewport rectangle (where the 3D camera is looking). */
  int vtx = 0, vty = 0, vhalf = 0;
  irrg_map3d_camera_view(&vtx, &vty, &vhalf);
  int cx = g_mm_x + (vtx * MM) / mw;
  int cy = g_mm_y + (vty * MM) / mh;
  int hw = (vhalf * MM) / mw;
  int hh = (vhalf * MM) / mh;
  static struct color c_vp  = { 255, 255, 255 };
  static struct color c_bdr = { 210, 210, 210 };
  /* Viewport as a thin OUTLINE (not a filled box) so the map shows through. */
  int vx0 = cx - hw, vy0 = cy - hh, vx1 = cx + hw, vy1 = cy + hh;
  canvas_put_rectangle(cv, &c_vp, vx0, vy0, vx1 - vx0 + 1, 2);       /* top    */
  canvas_put_rectangle(cv, &c_vp, vx0, vy1 - 1, vx1 - vx0 + 1, 2);   /* bottom */
  canvas_put_rectangle(cv, &c_vp, vx0, vy0, 2, vy1 - vy0 + 1);       /* left   */
  canvas_put_rectangle(cv, &c_vp, vx1 - 1, vy0, 2, vy1 - vy0 + 1);   /* right  */
  canvas_put_rectangle(cv, &c_bdr, g_mm_x, g_mm_y, MM, 1);          /* top    */
  canvas_put_rectangle(cv, &c_bdr, g_mm_x, g_mm_y + MM - 1, MM, 1); /* bottom */
  canvas_put_rectangle(cv, &c_bdr, g_mm_x, g_mm_y, 1, MM);          /* left   */
  canvas_put_rectangle(cv, &c_bdr, g_mm_x + MM - 1, g_mm_y, 1, MM); /* right  */
}

/* Hit-test the minimap box; on a hit set *out_tx/*out_ty to the clicked map
 * tile (for click-to-jump). Returns 1 if inside the box. */
int irrg_minimap_hit(int x, int y, int *out_tx, int *out_ty)
{
  if (g_mm_s <= 0) return 0;
  if (x < g_mm_x || x >= g_mm_x + g_mm_s || y < g_mm_y || y >= g_mm_y + g_mm_s)
    return 0;
  int mw = wld.map.xsize, mh = wld.map.ysize;
  if (mw <= 0 || mh <= 0) return 0;
  *out_tx = (int)((long)(x - g_mm_x) * mw / g_mm_s);
  *out_ty = (int)((long)(y - g_mm_y) * mh / g_mm_s);
  return 1;
}

void irrg_draw_dialogs(struct canvas *cv)
{
  if (!cv) return;
  irrg_draw_city_dialog(cv);
  irrg_draw_minimap(cv);
  /* The persistent bottom-left message window moved to the "Messages" submenu
   * (irrg_draw_messages_panel), freeing that corner for the unit info dialog. */
  if (g_report_open) irrg_draw_report(cv);
  if (g_help_open)   irrg_draw_help(cv);
  if (g_log_open)    irrg_draw_messages_panel(cv);
  /* Persistent shortcut hint (in-game only): keeps every action discoverable,
   * not just a hidden key. ESC opens the in-game menu (the full action list). */
  if (client_state() == C_S_RUNNING) {
    static struct color c_hinttx = { 200, 205, 215 };
    static struct color c_hintbg = { 24, 26, 32 };
    static struct color c_hinthy = { 130, 215, 130 };
    const char *s = "Menu + End Turn: top-right   |   click unit: select, click tile: move   |   drag: pan, wheel: zoom";
    int tw = 0, th = 0;
    irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT, s);
    canvas_put_rectangle(cv, &c_hintbg, 5, 6, tw + 6, th + 4);
    canvas_put_text(cv, 8, 8, FONT_REQTREE_TEXT, &c_hinttx, s);

    /* Persistent unit/city counter: always shows how many you have, so an
     * empty-looking (fog-covered) map can't be mistaken for 'no units/cities'.
     * With no city, hint at the first thing to do (found one). */
    const struct player *me = client_player();
    int nu = (me && me->units)  ? unit_list_size(me->units)  : 0;
    int nc = (me && me->cities) ? city_list_size(me->cities) : 0;
    char line2[160];
    if (nc == 0)
      std::snprintf(line2, sizeof(line2),
                    "Units: %d   Cities: 0  -  move a settler onto land, then found a city",
                    nu);
    else
      std::snprintf(line2, sizeof(line2), "Units: %d   Cities: %d", nu, nc);
    int tw2 = 0, th2 = 0;
    irrg_get_text_size(&tw2, &th2, FONT_REQTREE_TEXT, line2);
    canvas_put_rectangle(cv, &c_hintbg, 5, 6 + th + 6, tw2 + 6, th2 + 4);
    canvas_put_text(cv, 8, 6 + th + 8, FONT_REQTREE_TEXT, &c_hinthy, line2);
  }

  /* End Turn + Menu buttons (top-right) -- always available in-game; the Menu
   * button is the mouse entry point to every in-game action (no ESC needed). */
  irrg_draw_endturn_button(cv);
  irrg_draw_menu_button(cv);

  /* Selected-unit info panel (bottom-left corner) + the unit action buttons
   * (bottom centre) for the focused unit. Both work in the 3D and 2D views and
   * are no-ops unless a unit is focused. */
  irrg_draw_unit_dialog(cv);
  irrg_unitbar_draw(cv);

  /* (minimap is drawn up-front, after the city dialog, so it sits under the
   * top-right buttons but stays visible over the map) */

  /* FC_IRR_PICKGRID: overlay the screen->tile pick at a grid of points, so you
   * can SEE whether the picked tile lines up with the rendered tile under each
   * point (a pick/geometry mismatch shows up as labels sitting on the wrong
   * tiles). Diagnostic only. */
  if (std::getenv("FC_IRR_PICKGRID") && irrg_map3d_is_built() && cv->width > 0) {
    static struct color c_g = { 255, 240, 0 };
    static struct color c_gb = { 0, 0, 0 };
    for (int my = 90; my < cv->height - 60; my += 90) {
      for (int mx = 70; mx < cv->width - 70; mx += 110) {
        int tx = -1, ty = -1;
        if (irrg_map3d_pick(mx, my, &tx, &ty)) {
          char lbl[24];
          std::snprintf(lbl, sizeof(lbl), "%d,%d", tx, ty);
          int lw = 0, lh = 0;
          irrg_get_text_size(&lw, &lh, FONT_REQTREE_TEXT, lbl);
          canvas_put_rectangle(cv, &c_gb, mx, my - lh / 2, lw + 2, lh + 1);
          canvas_put_text(cv, mx + 1, my - lh / 2, FONT_REQTREE_TEXT, &c_g, lbl);
        }
      }
    }
  }
}

/* Present the dialogs as a transparent 2D overlay. Used on the 3D path, where
 * drawing onto the map canvas would be covered by the 3D scene. The overlay
 * canvas has a transparent (A=0) background, so irrg_canvas_present's alpha
 * blend keeps only the dialog pixels. */
void irrg_present_dialog_overlay(int w, int h)
{
  static struct canvas *ov = nullptr;
  if (w <= 0 || h <= 0) return;
  if (!ov || ov->width != w || ov->height != h) {
    if (ov) irrg_canvas_free(ov);
    ov = irrg_canvas_create(w, h);
    if (!ov) return;
  }
  std::memset(ov->pixels, 0, (size_t)w * h * 4);   /* transparent */
  irrg_draw_dialogs(ov);
  irrg_canvas_present(ov);
}
