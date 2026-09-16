/***********************************************************************
 Freeciv - gui-irrlicht in-game Menu.

 A modal full-window screen that is the GUI entry point for every in-game
 action. Opened with ESC while in the game (and no other dialog is up).
 Each row performs its action and closes the menu; ESC also closes it.
 No dead ends: "Quit to Main Menu" + "Close" always available.
***********************************************************************/
#include "gui_main.h"
#include "irrg_gamemenu.h"
#include "irrg_cxxside.h"  /* irrg_canvas_put_text/rectangle, irrg_get_text_size */
#include "graphics.h"      /* struct color {r,g,b}, struct canvas (concrete defs) */
#include "canvas_g.h"      /* client_font */
#include "irrg_dialogs.h"  /* irrg_reports_open(), irrg_help_open() */
#include "client_main.h"   /* client (global struct civclient), client_state() */
#include "control.h"       /* request_toggle_fog_of_war(), user_ended_turn() */
#include "connection.h"    /* connection_close() */

#include <cstdio>
#include <string>

/* Irrlicht keycodes (EKEY). */
enum {
  K_RETURN = 0x0d, K_ESCAPE = 0x1b, K_SPACE = 0x20,
  K_UP = 0x26, K_DOWN = 0x28, K_LEFT = 0x25, K_RIGHT = 0x27
};

enum {
  ROW_UNITS = 0, ROW_ECON, ROW_SCI, ROW_HELP, ROW_MSG, ROW_GRAPHICS, ROW_FOG,
  ROW_ENDTURN, ROW_QUIT, ROW_CLOSE, ROW_COUNT
};

static bool  g_active = false;
static int   g_W = 900, g_H = 600;
static int   g_sel    = ROW_UNITS;
static bool  g_fog    = true;
static bool  g_close  = false;

static struct color c_bg      = { 18, 20, 28 };
static color c_title = { 120, 200, 255 };
static struct color c_row     = { 40, 46, 62 };
static struct color c_rowsel  = { 70, 122, 195 };
static struct color c_quit    = { 60, 40, 40 };
static struct color c_quitssel = { 150, 70, 70 };
static struct color c_tx      = { 235, 235, 235 };
static struct color c_key     = { 130, 225, 130 };
static struct color c_val     = { 130, 225, 130 };
static struct color c_hint    = { 130, 132, 145 };

static void row_rect(int i, int *x, int *y, int *w, int *h)
{
  const int rw = 560, rh = 42, gap = 50;
  const int total = ROW_COUNT * gap;
  *x = g_W / 2 - rw / 2;
  *y = g_H / 2 - total / 2 + i * gap;
  *w = rw;
  *h = rh;
}

/* Perform the selected action and close the menu. */
static void activate_row(void)
{
  switch (g_sel) {
  case ROW_UNITS:    irrg_reports_open(0);  break; /* units  */
  case ROW_ECON:     irrg_reports_open(2);  break; /* economy */
  case ROW_SCI:      irrg_reports_open(1);  break; /* science */
  case ROW_HELP:     irrg_help_open();      break;
  case ROW_MSG:      irrg_log_open();       break; /* the game log panel */
  case ROW_GRAPHICS: irrg_open_settings();  break; /* transitions to the settings menu */
  case ROW_FOG:      request_toggle_fog_of_war(); break;
  case ROW_ENDTURN:  user_ended_turn();     break;
  case ROW_QUIT:
    connection_close(&client.conn, "quit to main menu (in-game menu)");
    break;
  case ROW_CLOSE:    break;
  default: break;
  }
  g_close = true;   /* every action (and Close) closes the menu */
}

void irrg_gamemenu_update(bool active, int win_w, int win_h)
{
  bool was = g_active;
  g_active = active;
  if (win_w > 0) g_W = win_w;
  if (win_h > 0) g_H = win_h;
  if (active && !was)           /* sync live state when the menu opens */
    g_fog = irrg_fog_enabled();
  if (!active) { g_close = false; g_sel = ROW_UNITS; }
}

bool irrg_gamemenu_is_active(void) { return g_active; }
bool irrg_gamemenu_take_close(void) { bool r = g_close; g_close = false; return r; }

void irrg_gamemenu_on_key(int key)
{
  if (!g_active) return;
  switch (key) {
  case K_UP:   if (g_sel > 0) g_sel--; break;
  case K_DOWN: if (g_sel < ROW_COUNT - 1) g_sel++; break;
  case K_RETURN:
  case K_SPACE: activate_row(); break;
  case K_ESCAPE: g_close = true; break;
  default: break;
  }
}

void irrg_gamemenu_on_click(int x, int y)
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

void irrg_gamemenu_draw(struct canvas *cv, int win_w, int win_h)
{
  if (!cv) return;
  g_W = win_w > 0 ? win_w : g_W;
  g_H = win_h > 0 ? win_h : g_H;

  irrg_canvas_put_rectangle(cv, &c_bg, 0, 0, g_W, g_H);

  {
    int tw = 0, th = 0;
    irrg_get_text_size(&tw, &th, FONT_CITY_NAME, "Menu");
    irrg_canvas_put_text(cv, g_W / 2 - tw / 2, g_H / 2 - 280,
                         FONT_CITY_NAME, &c_title, "Menu");
  }

  /* label + shortcut key per row. */
  const char *label[ROW_COUNT] = {
    "Unit Reports", "Economy Report", "Science Report", "Help", "Messages (log)",
    "Graphics Settings", "Fog of War", "End Turn", "Quit to Main Menu", "Close"
  };
  const char *key[ROW_COUNT] = {
    "R", "O", "S", "H", "L", "G", "F", "", "Q", "ESC"
  };

  for (int i = 0; i < ROW_COUNT; i++) {
    int rx, ry, rw, rh;
    row_rect(i, &rx, &ry, &rw, &rh);

    struct color *fill;
    if (i == ROW_QUIT)        fill = (g_sel == i) ? &c_quitssel : &c_quit;
    else                      fill = (g_sel == i) ? &c_rowsel : &c_row;
    irrg_canvas_put_rectangle(cv, fill, rx, ry, rw, rh);

    int tw = 0, th = 0;
    irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT, label[i]);
    irrg_canvas_put_text(cv, rx + 16, ry + (rh - th) / 2,
                         FONT_REQTREE_TEXT, &c_tx, label[i]);
    if (g_sel == i)
      irrg_canvas_put_text(cv, rx + 4, ry + (rh - th) / 2,
                           FONT_REQTREE_TEXT, &c_tx, ">");

    /* shortcut key (right side) */
    if (key[i][0]) {
      int kw = 0, kh = 0;
      irrg_get_text_size(&kw, &kh, FONT_REQTREE_TEXT, key[i]);
      irrg_canvas_put_text(cv, rx + rw - 16 - kw, ry + (rh - kh) / 2,
                           FONT_REQTREE_TEXT, &c_key, key[i]);
    }

    /* fog on/off value (to the left of the shortcut key) */
    if (i == ROW_FOG) {
      char v[16];
      std::snprintf(v, sizeof(v), g_fog ? "On" : "Off");
      int vw = 0, vh = 0, kw = 0, kh = 0;
      irrg_get_text_size(&vw, &vh, FONT_REQTREE_TEXT, v);
      irrg_get_text_size(&kw, &kh, FONT_REQTREE_TEXT, "F");
      irrg_canvas_put_text(cv, rx + rw - 16 - kw - 8 - vw,
                           ry + (rh - vh) / 2, FONT_REQTREE_TEXT, &c_val, v);
    }
  }

  {
    int tw = 0, th = 0;
    irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT,
                       "click a row    (or UP/DOWN + ENTER)    ESC close");
    irrg_canvas_put_text(cv, g_W / 2 - tw / 2, g_H - 44,
                         FONT_REQTREE_TEXT, &c_hint,
                       "click a row    (or UP/DOWN + ENTER)    ESC close");
  }
}
