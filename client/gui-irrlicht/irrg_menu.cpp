#include "gui_main.h"
#include "irrg_menu.h"
#include "irrg_cxxside.h"  /* irrg_canvas_put_text/rectangle, irrg_get_text_size */
#include "irrg_theme.h"    /* Civ4 palette + beveled frame/button helpers */
#include "graphics.h"      /* struct color {r,g,b}, struct canvas (concrete defs) */
#include "canvas_g.h"      /* client_font */
#include "client_main.h"   /* server_host, server_port, user_name */

#include <cstdio>
#include <cstring>
#include <string>

/* Irrlicht keycodes (EKEY) we handle for menu navigation. */
enum {
  K_RETURN = 0x0d, K_ESCAPE = 0x1b, K_SPACE = 0x20,
  K_UP = 0x26, K_DOWN = 0x28,
  K_1 = 0x31, K_2 = 0x32, K_3 = 0x33, K_Q = 0x51
};

enum { OPT_CONNECT = 0, OPT_OBSERVE = 1, OPT_QUIT = 2, OPT_COUNT = 3 };

static int  g_sel       = OPT_CONNECT;
static bool g_active    = false;
static int  g_W = 900, g_H = 600;
static bool g_connect   = false;
static bool g_observer  = false;
static bool g_quit      = false;

static void opt_rect(int i, int *x, int *y, int *w, int *h)
{
  const int bw = 460, bh = 46;
  *x = g_W / 2 - bw / 2;
  *y = g_H / 2 - 80 + i * 60;
  *w = bw;
  *h = bh;
}

void irrg_menu_update(bool active, int win_w, int win_h)
{
  g_active = active;
  if (win_w  > 0) g_W = win_w;
  if (win_h  > 0) g_H = win_h;
  if (!active) { g_connect = false; g_observer = false; g_quit = false; }
}

bool irrg_menu_is_active(void) { return g_active; }

bool irrg_menu_take_connect(void)  { bool r = g_connect;  g_connect  = false; return r; }
bool irrg_menu_take_observer(void) { bool r = g_observer; g_observer = false; return r; }
bool irrg_menu_take_quit(void)     { bool r = g_quit;     g_quit     = false; return r; }

static void activate(int i)
{
  switch (i) {
  case OPT_CONNECT: g_connect  = true; break;
  case OPT_OBSERVE: g_observer = true; break;
  default:          g_quit     = true; break;
  }
}

void irrg_menu_on_key(int key)
{
  if (!g_active) return;
  switch (key) {
  case K_UP:   if (g_sel > 0) g_sel--; break;
  case K_DOWN: if (g_sel < OPT_COUNT - 1) g_sel++; break;
  case K_RETURN:
  case K_SPACE:
    activate(g_sel);
    break;
  case K_1:  activate(OPT_CONNECT); break;
  case K_2:  activate(OPT_OBSERVE); break;
  case K_3:
  case K_Q:
  case K_ESCAPE:
    g_quit = true;
    break;
  default:
    break;
  }
}

void irrg_menu_on_click(int x, int y)
{
  if (!g_active) return;
  for (int i = 0; i < OPT_COUNT; i++) {
    int rx, ry, rw, rh;
    opt_rect(i, &rx, &ry, &rw, &rh);
    if (x >= rx && x < rx + rw && y >= ry && y < ry + rh) {
      g_sel = i;
      activate(i);
      return;
    }
  }
}

void irrg_menu_draw(struct canvas *cv, int win_w, int win_h)
{
  if (!cv) return;
  g_W = win_w > 0 ? win_w : g_W;
  g_H = win_h > 0 ? win_h : g_H;
  struct irrg_civ_pal P = irrg_civ();

  /* Full background (Civ4 dark charcoal). */
  irrg_canvas_put_rectangle(cv, &P.bg, 0, 0, g_W, g_H);

  /* Large gold title + subtitle. */
  {
    int tw = 0, th = 0;
    irrg_get_text_size(&tw, &th, FONT_CITY_NAME, "FreeCiv 3D");
    irrg_canvas_put_text(cv, g_W / 2 - tw / 2, g_H / 4 - 30,
                         FONT_CITY_NAME, &P.gold_hi, "FreeCiv 3D");
    irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT, "Irrlicht 3D client");
    irrg_canvas_put_text(cv, g_W / 2 - tw / 2, g_H / 4 + 14,
                         FONT_REQTREE_TEXT, &P.text_dim, "Irrlicht 3D client");
  }

  /* Central beveled frame around the option buttons. */
  {
    const int bw = 460, bh = 46, gap = 60;
    const int fw = bw + 40, fx = g_W / 2 - fw / 2;
    const int fy = g_H / 2 - 80 - 22;
    const int fh = gap * (OPT_COUNT - 1) + bh + 40;
    irrg_civ_frame(cv, fx, fy, fw, fh);
  }

  /* Options as beveled Civ4 buttons (selected one is the lighter hover fill). */
  const char *host = server_host[0] ? server_host : "localhost";
  int port = (server_port > 0) ? server_port : 3000;
  for (int i = 0; i < OPT_COUNT; i++) {
    int rx, ry, rw, rh;
    opt_rect(i, &rx, &ry, &rw, &rh);
    char label[200];
    if (i == OPT_CONNECT)
      std::snprintf(label, sizeof(label), "Connect to %s:%d  (as player)", host, port);
    else if (i == OPT_OBSERVE)
      std::snprintf(label, sizeof(label), "Observe %s:%d  (full map)", host, port);
    else
      std::snprintf(label, sizeof(label), "Quit");
    irrg_civ_button(cv, rx, ry, rw, rh, label, g_sel == i, false);
  }

  /* Hint bar. */
  {
    int tw = 0, th = 0;
    const char *hint = "1 connect   2 observer   3/ESC quit   (or click an option)";
    irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT, hint);
    irrg_canvas_put_text(cv, g_W / 2 - tw / 2, g_H - 46,
                         FONT_REQTREE_TEXT, &P.text_dim, hint);
  }
}
