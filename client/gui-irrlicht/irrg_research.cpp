#include "gui_main.h"
#include "irrg_research.h"
#include "irrg_theme.h"     /* Civ4 palette + beveled panel helper */
#include "irrg_cxxside.h"   /* irrg_canvas_put_text/rectangle, irrg_get_text_size */
#include "graphics.h"       /* struct color */
#include "canvas_g.h"       /* client_font (FONT_*) */
#include "client_main.h"    /* client (struct civclient { struct connection conn; ... }) */
#include "player.h"         /* client_player */
#include "research.h"       /* research_get, research_invention_state, names */
#include "tech.h"           /* advance_iterate, advance_by_number, names, Tech_type_id */
#include "packets_gen.h"    /* dsend_packet_player_research */

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

/* Number of tech rows visible at once in the list. */
#define RSHOW 15

/* Irrlicht EKEY codes we handle (no header constant needed for these). */
enum {
  K_RETURN = 0x0d, K_ESCAPE = 0x1b, K_SPACE = 0x20,
  K_UP = 0x26, K_DOWN = 0x28
};

struct rrow { int tech; std::string name; };

static bool g_open = false;
static std::vector<struct rrow> g_rows;
static int g_sel = 0, g_scroll = 0;

/* Geometry (filled in irrg_research_draw, used by the click handler so the
 * hit-test always matches what is on screen). */
static int g_list_x = 0, g_list_y = 0, g_list_w = 0;
static int g_close_x = 0, g_close_y = 0, g_close_w = 0, g_close_h = 0;

/* Rebuild the "can research next" list from the client's research state. */
static void rebuild(void)
{
  g_rows.clear();
  struct player *me = client_player();
  if (!me) return;
  struct research *res = research_get(me);
  if (!res) return;
  struct advance *padv;
  advance_iterate(padv) {
    Tech_type_id tech = padv->item_number;
    if (research_invention_state(res, tech) == TECH_PREREQS_KNOWN)
      g_rows.push_back({ tech, research_advance_name_translation(res, tech) });
  } advance_iterate_end;
  g_sel = 0;
  g_scroll = 0;
}

static void pick_and_send(Tech_type_id tech)
{
  if (tech == A_NONE) return;
  dsend_packet_player_research(&client.conn, tech);
  g_open = false;
}

void irrg_research_open(void)
{
  if (g_open) return;
  rebuild();
  g_open = true;
}

void irrg_research_close(void) { g_open = false; }
bool irrg_research_is_open(void) { return g_open; }

void irrg_research_draw(struct canvas *cv)
{
  if (!g_open || !cv) return;
  struct irrg_civ_pal P = irrg_civ();

  const int bw = 430;
  const int list_h = RSHOW * 20;
  const int bh = 214 + list_h;
  const int bx = 14, by = 78;      /* top-left, clear of the header + hint block */

  int ty = irrg_civ_panel(cv, bx, by, bw, bh, "Research  (choose your next tech)") + 2;

  /* Close button (top-right of the panel). */
  g_close_w = 26; g_close_h = 22;
  g_close_x = bx + bw - g_close_w - 6; g_close_y = by + 4;
  {
    static struct color c_clb = { 66, 42, 46 };
    static struct color c_clt = { 245, 140, 150 };
    irrg_canvas_put_rectangle(cv, &c_clb, g_close_x, g_close_y, g_close_w, g_close_h);
    irrg_canvas_put_text(cv, g_close_x + 7, g_close_y + 3, FONT_REQTREE_TEXT, &c_clt, "X");
  }

  struct player *me = client_player();
  struct research *res = (me ? research_get(me) : nullptr);
  char line[200];
  if (res) {
    const char *rn = (res->researching != A_NONE && res->researching != A_UNKNOWN)
        ? research_advance_name_translation(res, res->researching) : "(none)";
    std::snprintf(line, sizeof(line), "Now researching: %s", rn);
    irrg_canvas_put_text(cv, bx + 12, ty, FONT_REQTREE_TEXT, &P.green, line);
    ty += 22;
    const char *gn = (res->tech_goal != A_NONE)
        ? research_advance_name_translation(res, res->tech_goal) : "(none)";
    std::snprintf(line, sizeof(line), "Tech goal: %s", gn);
    irrg_canvas_put_text(cv, bx + 12, ty, FONT_REQTREE_TEXT, &P.text_dim, line);
    ty += 22;
  }

  /* Section divider + label. */
  irrg_canvas_put_rectangle(cv, &P.gold_lo, bx + 8, ty, bw - 16, 1);
  ty += 8;
  std::snprintf(line, sizeof(line), "Can research next (%d):", (int)g_rows.size());
  irrg_canvas_put_text(cv, bx + 12, ty, FONT_REQTREE_TEXT, &P.gold_hi, line);
  ty += 22;

  /* List of researchable techs. */
  g_list_x = bx + 10; g_list_y = ty; g_list_w = bw - 20;
  if (g_rows.empty()) {
    irrg_canvas_put_text(cv, bx + 12, ty, FONT_REQTREE_TEXT, &P.text_dim,
                         "No techs available to research.");
  } else {
    for (int i = 0; i < RSHOW && (g_scroll + i) < (int)g_rows.size(); i++) {
      int idx = g_scroll + i;
      int ry = ty + i * 20;
      if (idx == g_sel)
        irrg_canvas_put_rectangle(cv, &P.btn_hi, g_list_x, ry, g_list_w, 18);
      bool is_cur = (res != nullptr && res->researching == g_rows[idx].tech);
      struct color *tc = (idx == g_sel) ? &P.hdr_tx : (is_cur ? &P.green : &P.text);
      char label[160];
      std::snprintf(label, sizeof(label), "%s%s",
                    is_cur ? ">> " : "   ", g_rows[idx].name.c_str());
      irrg_canvas_put_text(cv, g_list_x + 4, ry + 1, FONT_REQTREE_TEXT, tc, label);
    }
  }

  /* Footer hint. */
  irrg_canvas_put_text(cv, bx + 12, by + bh - 22, FONT_REQTREE_TEXT, &P.text_dim,
                       "[click a row: set research]   [X / Esc: close]");
}

bool irrg_research_handle_click(int mx, int my)
{
  if (!g_open) return false;
  /* Close button. */
  if (mx >= g_close_x && mx < g_close_x + g_close_w
      && my >= g_close_y && my < g_close_y + g_close_h) {
    g_open = false;
    return true;
  }
  /* Rows. */
  for (int i = 0; i < RSHOW; i++) {
    int ry = g_list_y + i * 20;
    if (my >= ry && my < ry + 18
        && mx >= g_list_x && mx < g_list_x + g_list_w) {
      int idx = g_scroll + i;
      if (idx >= 0 && idx < (int)g_rows.size())
        pick_and_send((Tech_type_id)g_rows[idx].tech);
      return true;
    }
  }
  /* Consume clicks anywhere over the panel so they don't fall through to the map. */
  return true;
}

bool irrg_research_handle_key(int key, bool press)
{
  if (!g_open) return false;
  if (!press) return false;      /* let key releases pass through */
  if (key == K_ESCAPE) { g_open = false; return true; }
  if (!g_rows.empty()) {
    if (key == K_UP)
      { if (g_sel > 0) g_sel--; }
    else if (key == K_DOWN)
      { if (g_sel < (int)g_rows.size() - 1) g_sel++; }
    else if (key == K_RETURN || key == K_SPACE) {
      pick_and_send((Tech_type_id)g_rows[g_sel].tech);
      return true;
    }
    /* Keep the selection inside the visible window. */
    if (g_sel < g_scroll) g_scroll = g_sel;
    else if (g_sel >= g_scroll + RSHOW) g_scroll = g_sel - RSHOW + 1;
  }
  return true;   /* swallow all keys while the panel is up */
}
