#include "gui_main.h"
#include "irrg_newgame.h"
#include "irrg_cxxside.h"  /* irrg_canvas_put_text/rectangle, irrg_get_text_size */
#include "graphics.h"      /* struct color {r,g,b}, struct canvas (concrete defs) */
#include "canvas_g.h"      /* client_font */
#include "client_main.h"   /* client_state() etc. */
#include "chatline_common.h" /* send_chat() -- send the /set + /start commands */
#include <cstdio>
#include <string>

/* Irrlicht keycodes (EKEY). */
enum {
  K_RETURN = 0x0d, K_ESCAPE = 0x1b, K_SPACE = 0x20,
  K_UP = 0x26, K_DOWN = 0x28, K_LEFT = 0x25, K_RIGHT = 0x27
};

enum {
  ROW_MAPSIZE = 0, ROW_WRAP = 1, ROW_AI = 2, ROW_DIFF = 3,
  ROW_START = 4, ROW_CANCEL = 5, ROW_COUNT = 6
};

/* Option data. The server's 'size' option is map area in THOUSANDS of tiles
 * (size=4 -> 4,000 tiles, the default normal map; max 2048). */
static const int    SIZES[4]   = { 2, 4, 8, 16 };
static const char  *MS_LABEL[4]= { "Small  (~2k tiles)", "Medium  (~4k tiles)",
                                   "Large  (~8k tiles)", "Huge  (~16k tiles)" };
static const char  *WRAP_LABEL[3] = { "Torus (wrap both ways)", "Horizontal (east-west)",
                                      "Vertical (north-south)" };
static const char  *WRAP_VAL[3]   = { "wrapx|wrapy", "wrapx", "wrapy" };
static const char  *DIFF_LABEL[4] = { "Easy", "Normal", "Hard", "Expert" };
static const char  *DIFF_CMD[4]   = { "/easy", "/normal", "/hard", "/cheating" };
static const int   AI_BASE = 6;   /* classic ruleset aifill default */

static bool  g_active = false;
static int   g_W = 900, g_H = 600;
static int   g_sel     = ROW_MAPSIZE;
static int   g_mapsize = 1;       /* Medium */
static int   g_wrap    = 0;       /* Torus */
static int   g_ai      = 6;
static int   g_diff    = 1;       /* Normal */
static bool  g_start   = false;
static bool  g_cancel  = false;

static struct color c_bg     = { 18, 20, 28 };
static struct color c_title  = { 120, 200, 255 };
static struct color c_row    = { 40, 46, 62 };
static struct color c_rowsel = { 70, 122, 195 };
static struct color c_start  = { 40, 110, 50 };
static struct color c_startsel = { 60, 160, 75 };
static struct color c_cancel = { 60, 40, 40 };
static struct color c_tx     = { 235, 235, 235 };
static struct color c_val    = { 130, 225, 130 };
static struct color c_hint   = { 130, 132, 145 };

static void row_rect(int i, int *x, int *y, int *w, int *h)
{
  const int rw = 560, rh = 44;
  *x = g_W / 2 - rw / 2;
  *y = g_H / 2 - 150 + i * 54;
  *w = rw;
  *h = rh;
}

void irrg_newgame_update(bool active, int win_w, int win_h)
{
  g_active = active;
  if (win_w > 0) g_W = win_w;
  if (win_h > 0) g_H = win_h;
  if (!active) { g_start = false; g_cancel = false; g_sel = ROW_MAPSIZE; }
}

bool irrg_newgame_is_active(void) { return g_active; }
bool irrg_newgame_take_start(void)  { bool r = g_start;  g_start  = false; return r; }
bool irrg_newgame_take_cancel(void) { bool r = g_cancel; g_cancel = false; return r; }

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void change(int dir)
{
  switch (g_sel) {
  case ROW_MAPSIZE: g_mapsize = clampi(g_mapsize + dir, 0, 3); break;
  case ROW_WRAP:    g_wrap    = clampi(g_wrap + dir, 0, 2);    break;
  case ROW_AI:      g_ai      = clampi(g_ai + dir, 0, 8);      break;
  case ROW_DIFF:    g_diff    = clampi(g_diff + dir, 0, 3);    break;
  default: break;
  }
}

static void activate_row(void)
{
  if (g_sel == ROW_START)  g_start  = true;
  else if (g_sel == ROW_CANCEL) g_cancel = true;
  else change(1);
}

void irrg_newgame_on_key(int key)
{
  if (!g_active) return;
  switch (key) {
  case K_UP:   if (g_sel > 0) g_sel--; break;
  case K_DOWN: if (g_sel < ROW_COUNT - 1) g_sel++; break;
  case K_LEFT:  if (g_sel <= ROW_DIFF) change(-1); break;
  case K_RIGHT: if (g_sel <= ROW_DIFF) change(1);  break;
  case K_RETURN:
  case K_SPACE: activate_row(); break;
  case K_ESCAPE:  g_cancel = true; break;
  default: break;
  }
}

void irrg_newgame_on_click(int x, int y)
{
  if (!g_active) return;
  for (int i = 0; i < ROW_COUNT; i++) {
    int rx, ry, rw, rh;
    row_rect(i, &rx, &ry, &rw, &rh);
    if (x >= rx && x < rx + rw && y >= ry && y < ry + rh) {
      g_sel = i;
      activate_row();
      return;
    }
  }
}

void irrg_newgame_send_options(void)
{
  char buf[128];
  /* Map size. */
  send_chat("/set mapsize fullsize");
  std::snprintf(buf, sizeof(buf), "/set size %d", SIZES[g_mapsize]);
  send_chat(buf);
  /* World wrap. */
  std::snprintf(buf, sizeof(buf), "/set wrap %s", WRAP_VAL[g_wrap]);
  send_chat(buf);
  /* AI difficulty (applies to all AI players). */
  send_chat(DIFF_CMD[g_diff]);
  /* AI count (adjust from the classic aifill=6 baseline). */
  if (g_ai < AI_BASE)
    for (int i = g_ai + 1; i <= AI_BASE; i++) {
      std::snprintf(buf, sizeof(buf), "/remove AI*%d", i);
      send_chat(buf);
    }
  else if (g_ai > AI_BASE)
    for (int i = AI_BASE + 1; i <= g_ai; i++) {
      std::snprintf(buf, sizeof(buf), "/create AI*%d", i);
      send_chat(buf);
    }
  /* Start the game. */
  send_chat("/start");
}

void irrg_newgame_test_send(int mapsize, int wrap, int ai, int diff)
{
  g_mapsize = clampi(mapsize, 0, 3);
  g_wrap    = clampi(wrap, 0, 2);
  g_ai      = clampi(ai, 0, 8);
  g_diff    = clampi(diff, 0, 3);
  irrg_newgame_send_options();
}

void irrg_newgame_draw(struct canvas *cv, int win_w, int win_h)
{
  if (!cv) return;
  g_W = win_w > 0 ? win_w : g_W;
  g_H = win_h > 0 ? win_h : g_H;

  irrg_canvas_put_rectangle(cv, &c_bg, 0, 0, g_W, g_H);

  {
    int tw = 0, th = 0;
    irrg_get_text_size(&tw, &th, FONT_CITY_NAME, "Start a New Game");
    irrg_canvas_put_text(cv, g_W / 2 - tw / 2, g_H / 4 - 24,
                         FONT_CITY_NAME, &c_title, "Start a New Game");
  }
  {
    int tw = 0, th = 0;
    irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT,
                       "Configure the map, then start. (requires server command access)");
    irrg_canvas_put_text(cv, g_W / 2 - tw / 2, g_H / 4 + 4,
                         FONT_REQTREE_TEXT, &c_hint,
                       "Configure the map, then start. (requires server command access)");
  }

  const char *field[ROW_COUNT] = { "Map size", "World wrap", "AI opponents",
                                   "AI difficulty", "Start Game", "Cancel" };
  for (int i = 0; i < ROW_COUNT; i++) {
    int rx, ry, rw, rh;
    row_rect(i, &rx, &ry, &rw, &rh);

    struct color *fill;
    if (i == ROW_START)       fill = (g_sel == i) ? &c_startsel : &c_start;
    else if (i == ROW_CANCEL) fill = (g_sel == i) ? &c_rowsel : &c_cancel;
    else                      fill = (g_sel == i) ? &c_rowsel : &c_row;
    irrg_canvas_put_rectangle(cv, fill, rx, ry, rw, rh);

    int tw = 0, th = 0;
    irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT, field[i]);
    if (i <= ROW_DIFF)
      irrg_canvas_put_text(cv, rx + 16, ry + (rh - th) / 2,
                           FONT_REQTREE_TEXT, &c_tx, field[i]);
    else
      irrg_canvas_put_text(cv, rx + rw / 2 - tw / 2, ry + (rh - th) / 2,
                           FONT_REQTREE_TEXT, &c_tx, field[i]);
    if (g_sel == i)
      irrg_canvas_put_text(cv, rx + 4, ry + (rh - th) / 2,
                           FONT_REQTREE_TEXT, &c_tx, ">");

    /* option value (right side) */
    if (i <= ROW_DIFF) {
      char v[40];
      if (i == ROW_MAPSIZE) std::snprintf(v, sizeof(v), "%s", MS_LABEL[g_mapsize]);
      else if (i == ROW_WRAP) std::snprintf(v, sizeof(v), "%s", WRAP_LABEL[g_wrap]);
      else if (i == ROW_AI) std::snprintf(v, sizeof(v), "%d", g_ai);
      else std::snprintf(v, sizeof(v), "%s", DIFF_LABEL[g_diff]);
      int vw = 0, vh = 0;
      irrg_get_text_size(&vw, &vh, FONT_REQTREE_TEXT, v);
      irrg_canvas_put_text(cv, rx + rw - 16 - vw, ry + (rh - vh) / 2,
                           FONT_REQTREE_TEXT, &c_val, v);
    }
  }

  {
    int tw = 0, th = 0;
    irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT,
                       "UP/DOWN select    LEFT/RIGHT change    ENTER start/apply    ESC cancel");
    irrg_canvas_put_text(cv, g_W / 2 - tw / 2, g_H - 44,
                         FONT_REQTREE_TEXT, &c_hint,
                       "UP/DOWN select    LEFT/RIGHT change    ENTER start/apply    ESC cancel");
  }
}
