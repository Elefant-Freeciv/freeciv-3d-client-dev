#include "gui_main.h"
#include "irrg_settings.h"
#include "irrg_cxxside.h"  /* irrg_canvas_put_text/rectangle, irrg_get_text_size */
#include "graphics.h"      /* struct color {r,g,b}, struct canvas (concrete defs) */
#include "canvas_g.h"      /* client_font */
#include <cstdio>
#include <string>

/* Irrlicht keycodes (EKEY). */
enum {
  K_RETURN = 0x0d, K_ESCAPE = 0x1b, K_SPACE = 0x20,
  K_UP = 0x26, K_DOWN = 0x28, K_LEFT = 0x25, K_RIGHT = 0x27
};

enum { ROW_ZOOM = 0, ROW_FOG = 1, ROW_3D = 2, ROW_COUNT = 3 };

static bool  g_active = false;
static int   g_W = 900, g_H = 600;
static int   g_sel  = ROW_ZOOM;
static float g_zoom = 2.0f;
static bool  g_fog  = true;
static bool  g_use3d = true;
static bool  g_close = false;

static struct color c_bg      = { 18, 20, 28 };
static struct color c_title   = { 120, 200, 255 };
static struct color c_row     = { 40, 46, 62 };
static struct color c_rowsel  = { 70, 122, 195 };
static struct color c_tx      = { 235, 235, 235 };
static struct color c_val     = { 130, 225, 130 };
static struct color c_hint    = { 130, 132, 145 };

static void row_rect(int i, int *x, int *y, int *w, int *h)
{
  const int rw = 540, rh = 46;
  *x = g_W / 2 - rw / 2;
  *y = g_H / 2 - 120 + i * 58;
  *w = rw;
  *h = rh;
}

void irrg_settings_update(bool active, int win_w, int win_h)
{
  bool was = g_active;
  g_active = active;
  if (win_w > 0) g_W = win_w;
  if (win_h > 0) g_H = win_h;
  if (active && !was) {           /* sync from live state when the menu opens */
    g_zoom  = (float)irrg_get_map_zoom_present();
    g_fog   = irrg_fog_enabled();
    g_use3d = irrg_use3d();
  }
}

bool irrg_settings_is_active(void) { return g_active; }
bool irrg_settings_take_close(void) { bool r = g_close; g_close = false; return r; }

static void change(int dir)
{
  switch (g_sel) {
  case ROW_ZOOM:
    g_zoom += dir * 0.5f;
    if (g_zoom < 1.0f) g_zoom = 1.0f;
    if (g_zoom > 4.0f) g_zoom = 4.0f;
    irrg_set_map_zoom_present((double)g_zoom);
    break;
  case ROW_FOG:
    g_fog = !g_fog;
    irrg_set_fog_enabled(g_fog);
    break;
  case ROW_3D:
    g_use3d = !g_use3d;
    irrg_set_use3d(g_use3d);   /* builds the 3D map on the fly if needed */
    break;
  }
}

void irrg_settings_on_key(int key)
{
  if (!g_active) return;
  switch (key) {
  case K_UP:   if (g_sel > 0) g_sel--; break;
  case K_DOWN: if (g_sel < ROW_COUNT - 1) g_sel++; break;
  case K_LEFT:  change(-1); break;
  case K_RIGHT: change(1);  break;
  case K_RETURN:
  case K_SPACE: change(1);  break;   /* toggle/step the selected option */
  case K_ESCAPE:  g_close = true; break;
  default: break;
  }
}

void irrg_settings_on_click(int x, int y)
{
  if (!g_active) return;
  for (int i = 0; i < ROW_COUNT; i++) {
    int rx, ry, rw, rh;
    row_rect(i, &rx, &ry, &rw, &rh);
    if (x >= rx && x < rx + rw && y >= ry && y < ry + rh) {
      g_sel = i;
      change(1);                     /* clicking a row toggles/steps it */
      return;
    }
  }
}

static void draw_value(struct canvas *cv, int x, int y, const char *s)
{
  int tw = 0, th = 0;
  irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT, s);
  irrg_canvas_put_text(cv, x, y, FONT_REQTREE_TEXT, &c_val, s);
}

void irrg_settings_draw(struct canvas *cv, int win_w, int win_h)
{
  if (!cv) return;
  g_W = win_w > 0 ? win_w : g_W;
  g_H = win_h > 0 ? win_h : g_H;

  irrg_canvas_put_rectangle(cv, &c_bg, 0, 0, g_W, g_H);

  {
    int tw = 0, th = 0;
    irrg_get_text_size(&tw, &th, FONT_CITY_NAME, "Graphics Settings");
    irrg_canvas_put_text(cv, g_W / 2 - tw / 2, g_H / 4 - 20,
                         FONT_CITY_NAME, &c_title, "Graphics Settings");
  }

  const char *labels[ROW_COUNT] = { "2D Map Zoom", "Fog of War", "3D Terrain View" };
  for (int i = 0; i < ROW_COUNT; i++) {
    int rx, ry, rw, rh;
    row_rect(i, &rx, &ry, &rw, &rh);
    irrg_canvas_put_rectangle(cv, (g_sel == i) ? &c_rowsel : &c_row, rx, ry, rw, rh);

    int tw = 0, th = 0;
    irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT, labels[i]);
    irrg_canvas_put_text(cv, rx + 16, ry + (rh - th) / 2,
                         FONT_REQTREE_TEXT, &c_tx, labels[i]);
    if (g_sel == i)
      irrg_canvas_put_text(cv, rx + 4, ry + (rh - th) / 2,
                           FONT_REQTREE_TEXT, &c_tx, ">");

    /* value (right-aligned) */
    char v[40];
    if (i == ROW_ZOOM) {
      std::snprintf(v, sizeof(v), "%.1fx", (double)g_zoom);
    } else if (i == ROW_FOG) {
      std::snprintf(v, sizeof(v), g_fog ? "On" : "Off");
    } else {
      std::snprintf(v, sizeof(v), g_use3d ? "On" : "Off");
    }
    int vw = 0, vh = 0;
    irrg_get_text_size(&vw, &vh, FONT_REQTREE_TEXT, v);
    irrg_canvas_put_text(cv, rx + rw - 16 - vw, ry + (rh - vh) / 2,
                         FONT_REQTREE_TEXT, &c_val, v);
  }

  {
    int tw = 0, th = 0;
    irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT,
                       "UP/DOWN select    LEFT/RIGHT or click to change    ESC to close");
    irrg_canvas_put_text(cv, g_W / 2 - tw / 2, g_H - 44,
                         FONT_REQTREE_TEXT, &c_hint,
                       "UP/DOWN select    LEFT/RIGHT or click to change    ESC to close");
  }
}
