/***********************************************************************
 Freeciv - gui-irrlicht Phase 6: interactive map controls.
   Maps Irrlicht mouse input onto FreeCiv's shared mapctrl core and provides
   a headless unit-move verification (FC_IRR_SIMMOVE).

   Irrlicht 1.8.5 delivers input through an IEventReceiver (registered once
   with device->setEventReceiver); there is no device->getEvent(). The
   receiver's OnEvent() is invoked by device->run() for each OS event.

   The move path mirrors what a real mouse-driven goto does in gui-sdl2:
     select a unit  -> unit_focus_set_and_select()
     enter goto     -> request_unit_goto(ORDER_LAST, ACTION_NONE, -1)
     commit route   -> do_unit_goto(dest_tile); clear_hover_state()
   and a plain left-click is do_map_click(tile, SELECT_POPUP).
***********************************************************************/
#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <string>

#include <irrlicht.h>

#include "irrg_dialogs.h"      /* Phase 6: close city dialog on ESC */
#include "irrg_menu.h"         /* pre-game main menu: route input to it when active */
#include "irrg_settings.h"     /* in-game graphics settings menu */
#include "irrg_newgame.h"      /* new-game options screen */
#include "irrg_gamemenu.h"     /* in-game menu (ESC with no dialog open) */
#include "gui_main.h"          /* irrg_open_settings / irrg_open_newgame */
#include "world_object.h"      /* extern struct world wld (wld.map) */
#include "map.h"               /* map_pos_to_tile, index_to_map_pos_x/y */
#include "tile.h"              /* struct tile, tile_index */
#include "unit.h"              /* struct unit */
#include "unitlist.h"          /* unit_list_iterate */
#include "city.h"              /* struct city (name/tile/id), city_list_iterate */
#include "player.h"            /* struct player */
#include "actions.h"           /* ACTION_NONE */
#include "client_main.h"       /* client_player(), client_state(), client (global) */
#include "chatline_common.h"   /* send_chat() -- "start a game" from the preparing screen */
#include "connection.h"        /* connection_close() -- disconnect from the preparing screen */
#include "control.h"           /* do_map_click, request_unit_goto, do_unit_goto,
                                 clear_hover_state, get_num_units_in_focus,
                                 unit_focus_set_and_select */
#include "movement.h"          /* unit_can_move_to_tile */
#include "zoom.h"              /* mouse_zoom */
#include "irrg_cxxside.h"      /* irrg_canvas_put_rectangle (FC_IRR_MARKUNITS) */
#include "mapview_common.h"    /* canvas_pos_to_tile, center_tile_mapcanvas */
#include "graphics.h"
#include "irrg_map3d.h"
#include "irrg_unitbar.h"    /* irrg_unitbar_handle_click (unit action buttons) */
#include "irrg_interact.h"
#include "packets_gen.h"       /* dsend_packet_edit_city_create (test city) */

using namespace irr;

static void irrg_log(const char *msg)
{
  std::fprintf(stderr, "[irrg] %s\n", msg);
  std::fflush(stderr);
}

/* Convert a screen pixel to a map tile (3D ray-pick or 2D canvas mapping).
 *
 * For the 2D view we must invert whatever the present path did to the map
 * canvas. The default present (g_map_zoom_present > 1) is NOT "screen == canvas":
 * it shows a CENTERED CROP of the canvas, nearest-neighbor upscaled to fill the
 * window (see irrg_canvas_present_zoomed). A screen pixel therefore maps to a
 * different canvas pixel than (sx, sy); using (sx, sy) directly (the old code)
 * selects/moves/recenters the WRONG tile whenever the presentation zoom is in
 * effect -- an "odd behavior" bug. Only the 1:1 present (zoom <= 1) is identity. */
static struct tile *irrg_screen_to_tile(int sx, int sy)
{
  if (client_state() != C_S_RUNNING)
    return NULL;
  if (irrg_map3d_is_built()) {
    int tx = 0, ty = 0;
    if (!irrg_map3d_pick(sx, sy, &tx, &ty))
      return NULL;
    return map_pos_to_tile(&wld.map, tx, ty);
  }
  struct canvas *mc = mapview.store ? mapview.store : irrg_get_map_canvas();
  if (!mc) return canvas_pos_to_tile(sx, sy, mouse_zoom);
  int cx = sx, cy = sy;
  const float zoom = (float)irrg_get_map_zoom_present();
  if (zoom > 1.0001f) {
    irr::IrrlichtDevice *dev = (irr::IrrlichtDevice *)irrg_device();
    core::dimension2d<u32> ssz = (dev && dev->getVideoDriver())
        ? dev->getVideoDriver()->getScreenSize()
        : core::dimension2d<u32>((u32)mc->width, (u32)mc->height);
    const int win_w = (int)ssz.Width, win_h = (int)ssz.Height;
    if (win_w > 0 && win_h > 0) {
      /* Mirror irrg_canvas_present_zoomed's centered crop exactly. */
      int crop_w = (int)((float)mc->width  / zoom);
      int crop_h = (int)((float)mc->height / zoom);
      if (crop_w < 1) crop_w = 1;
      if (crop_h < 1) crop_h = 1;
      if (crop_w > mc->width)  crop_w = mc->width;
      if (crop_h > mc->height) crop_h = mc->height;
      const int cx0 = (mc->width  - crop_w) / 2;
      const int cy0 = (mc->height - crop_h) / 2;
      cx = cx0 + (int)((float)sx * (float)crop_w / (float)win_w);
      cy = cy0 + (int)((float)sy * (float)crop_h / (float)win_h);
    }
  }
  if (cx < 0) cx = 0; else if (cx >= mc->width)  cx = mc->width  - 1;
  if (cy < 0) cy = 0; else if (cy >= mc->height) cy = mc->height - 1;
  return canvas_pos_to_tile(cx, cy, mouse_zoom);
}

/* Handle a left (select) or right (move/recenter) click on the map. */
static void irrg_handle_map_click(int sx, int sy, bool left)
{
  /* Persistent top-right command buttons (checked first so they're never
   * swallowed by a map action): Menu opens the in-game menu (the mouse path to
   * every action); End Turn ends the turn. */
  if (left && irrg_menu_button_hit(sx, sy)) {
    irrg_open_gamemenu();
    if (getenv("FC_IRR_UBDBG")) { fprintf(stderr, "[irrg] Menu clicked\n"); fflush(stderr); }
    return;
  }
  if (left && irrg_endturn_button_hit(sx, sy)) {
    user_ended_turn();
    if (getenv("FC_IRR_UBDBG")) { fprintf(stderr, "[irrg] End Turn clicked\n"); fflush(stderr); }
    return;
  }

  /* City dialog (if open): a click on the build list starts that build, the
   * close button dismisses it; any click inside the panel is consumed so it
   * does not also select/move a unit on the map behind the dialog. */
  if (irrg_city_dialog_click(sx, sy, left))
    return;

  /* Report/Help/Log panels: a left click closes the topmost one (the mouse
   * equivalent of ESC), so they can be dismissed without a keyboard. */
  if (left && irrg_panel_click(sx, sy))
    return;

  /* Clicking the selected-unit dialog re-centres the 3D view on that unit. */
  if (left && irrg_unit_dialog_hit(sx, sy)) {
    irrg_map3d_refocus_current_unit();
    return;
  }

  struct tile *ptile = irrg_screen_to_tile(sx, sy);
  if (!ptile)
    return;
  if (getenv("FC_IRR_PICKCLICK")) {
    float wx = 0, wz = 0;
    if (irrg_map3d_pick_world(sx, sy, &wx, &wz))
      fprintf(stderr, "[irrg] PICKCLICK left=%d screen(%d,%d) -> tile(%d,%d) world(%.2f,%.2f)\n",
              left ? 1 : 0, sx, sy,
              index_to_map_pos_x(tile_index(ptile)),
              index_to_map_pos_y(tile_index(ptile)), wx, wz);
    fflush(stderr);
  }
  struct unit *fu = head_of_units_in_focus();
  if (left) {
    /* Click-to-move: with a unit focused, a left-click on an EMPTY tile (no
     * city, no other unit) moves the focused unit there. Clicks on your own
     * units / enemy stacks / cities still go through do_map_click (select,
     * stack popup, city dialog) so those keep their normal behaviour. A
     * right-click always moves to any tile (see below). */
    bool dest_busy = (tile_city(ptile) != NULL)
                    || (unit_list_size(ptile->units) > 0);
    if (fu && !dest_busy && tile_index(fu->tile) != tile_index(ptile)) {
      request_unit_goto(ORDER_LAST, ACTION_NONE, -1);
      do_unit_goto(ptile);
      clear_hover_state();
      if (getenv("FC_IRR_PICKCLICK")) {
        fprintf(stderr, "[irrg] MOVE(left) unit=%d -> tile(%d,%d)\n",
                fu->id, index_to_map_pos_x(tile_index(ptile)),
                index_to_map_pos_y(tile_index(ptile)));
        fflush(stderr);
      }
    } else {
      do_map_click(ptile, SELECT_POPUP);
    }
  } else if (get_num_units_in_focus() > 0) {
    /* Right-click: move the focused unit(s) to the clicked tile (goto). */
    struct unit *rfu = head_of_units_in_focus();
    request_unit_goto(ORDER_LAST, ACTION_NONE, -1);
    do_unit_goto(ptile);
    clear_hover_state();
    if (getenv("FC_IRR_PICKCLICK") && rfu) {
      fprintf(stderr, "[irrg] MOVE(right) unit=%d -> tile(%d,%d)\n",
              rfu->id, index_to_map_pos_x(tile_index(ptile)),
              index_to_map_pos_y(tile_index(ptile)));
      fflush(stderr);
    }
  } else {
    /* No unit selected: recenter the view on the tile. In the 3D view this
     * moves the 3D camera; in the 2D view it re-centers the map canvas. */
    if (irrg_map3d_is_built()) {
      irrg_map3d_center_on(index_to_map_pos_x(tile_index(ptile)),
                           index_to_map_pos_y(tile_index(ptile)));
    } else {
      center_tile_mapcanvas(ptile);
    }
  }
}

/* 3D camera grab-pan state. A middle-button drag, OR a left-button drag that
 * moves past a small threshold (a plain left press+release is still a select
 * click), translates the camera so the ground point under the cursor at drag
 * start stays fixed under the cursor. */
static bool  g_panning       = false;
static float g_pan_anchor_x  = 0.0f, g_pan_anchor_y = 0.0f;
static bool  g_left_down     = false;
static bool  g_left_dragging = false;
static int   g_left_start_x  = 0, g_left_start_y = 0;
static const int kPanDragThreshold = 5;   /* px before a left press becomes a pan */

static void irrg_pan_begin(int mx, int my)
{
  float ax = 0.0f, ay = 0.0f;
  if (irrg_map3d_pick_world(mx, my, &ax, &ay)) {
    g_pan_anchor_x = ax; g_pan_anchor_y = ay; g_panning = true;
  }
}

static void irrg_pan_step(int mx, int my)
{
  float nx = 0.0f, ny = 0.0f;
  if (irrg_map3d_pick_world(mx, my, &nx, &ny))
    irrg_map3d_pan(g_pan_anchor_x - nx, g_pan_anchor_y - ny);
}

/* Irrlicht 1.8.5 event receiver: handles mouse input on the map. */
class irrg_event_receiver : public irr::IEventReceiver {
public:
  virtual bool OnEvent(const irr::SEvent &event)
  {
    /* Modal full-window screens (new-game options, graphics settings) own ALL
     * input while active, so the map/dialog handlers below don't fire. */
    if (irrg_newgame_is_active() || irrg_settings_is_active()
        || irrg_gamemenu_is_active()) {
      if (event.EventType == irr::EET_KEY_INPUT_EVENT) {
        if (irrg_newgame_is_active())       irrg_newgame_on_key(event.KeyInput.Key);
        else if (irrg_settings_is_active()) irrg_settings_on_key(event.KeyInput.Key);
        else                                irrg_gamemenu_on_key(event.KeyInput.Key);
        return true;
      }
      if (event.EventType == irr::EET_MOUSE_INPUT_EVENT
          && event.MouseInput.Event == irr::EMIE_LMOUSE_LEFT_UP) {
        if (irrg_newgame_is_active())
          irrg_newgame_on_click(event.MouseInput.X, event.MouseInput.Y);
        else if (irrg_settings_is_active())
          irrg_settings_on_click(event.MouseInput.X, event.MouseInput.Y);
        else
          irrg_gamemenu_on_click(event.MouseInput.X, event.MouseInput.Y);
        return true;
      }
      return true;   /* swallow everything else while a modal screen is up */
    }

    /* PREPARING (connected, but no game running yet): no map exists. Own the
     * input here: ENTER opens the new-game options screen (choose the map, then
     * start); ESC disconnects (back to the main menu). The matching status
     * screen is drawn in gui_main.cpp's render loop. */
    if (client_state() == C_S_PREPARING
        && event.EventType == irr::EET_KEY_INPUT_EVENT
        && event.KeyInput.PressedDown) {
      switch (event.KeyInput.Key) {
      case irr::KEY_RETURN:
      case irr::KEY_KEY_1:
        /* Open the new-game options screen (map size / wrap / AI / difficulty);
         * "Start Game" there sends the options + /start. */
        irrg_open_newgame();
        irrg_log("preparing: ENTER -> opened new-game options screen");
        return true;
      case irr::KEY_ESCAPE:
        connection_close(&client.conn,
                         "client disconnected (from preparing screen)");
        irrg_log("preparing: ESC -> disconnecting");
        return true;
      default:
        return true;   /* swallow other keys while the preparing screen is up */
      }
    }

    /* PREPARING (mouse): click the on-screen buttons -- "Start a New Game"
     * opens the new-game options, "Disconnect" returns to the main menu. This
     * makes the whole pre-game flow reachable with a mouse alone. */
    if (client_state() == C_S_PREPARING
        && event.EventType == irr::EET_MOUSE_INPUT_EVENT
        && event.MouseInput.Event == irr::EMIE_LMOUSE_LEFT_UP) {
      int which = irrg_prep_button_hit(event.MouseInput.X, event.MouseInput.Y);
      if (which == 0) {
        irrg_open_newgame();
        irrg_log("preparing: click -> opened new-game options screen");
        return true;
      }
      if (which == 1) {
        connection_close(&client.conn,
                         "client disconnected (from preparing screen)");
        irrg_log("preparing: click -> disconnecting");
        return true;
      }
      return true;   /* swallow other clicks while the preparing screen is up */
    }

    /* While the pre-game main menu is up, it owns all input (keyboard + mouse)
     * so the map/dialog handlers below don't fire. */
    if (irrg_menu_is_active()) {
      if (event.EventType == irr::EET_KEY_INPUT_EVENT) {
        irrg_menu_on_key(event.KeyInput.Key);
        return true;
      }
      if (event.EventType == irr::EET_MOUSE_INPUT_EVENT
          && event.MouseInput.Event == irr::EMIE_LMOUSE_LEFT_UP) {
        irrg_menu_on_click(event.MouseInput.X, event.MouseInput.Y);
        return true;
      }
      return true;   /* swallow everything else while the menu is showing */
    }

    if (event.EventType == irr::EET_KEY_INPUT_EVENT) {
      const bool press = event.KeyInput.PressedDown;
      /* City production menu: while the city dialog is open, Up/Down/PgUp/
       * PgDn/Enter/Space drive it and are consumed (so they don't also pan the
       * map or fire the R/O/S/H shortcuts). */
      if (irrg_city_dialog_is_open() && press
          && irrg_city_dialog_handle_key(event.KeyInput.Key, true))
        return true;
      switch (event.KeyInput.Key) {
      case irr::KEY_ESCAPE:
        if (press) {
          /* No modal is up (handled above). If a dialog is open, the release
           * handler below will close it; otherwise open the in-game menu --
           * the single GUI entry point for every in-game action. */
          if (client_state() == C_S_RUNNING
              && !irrg_report_or_help_open() && !irrg_city_dialog_is_open())
            irrg_open_gamemenu();
          return true;
        }
        /* (release) Close the topmost dialog: report, then help, then city. */
        if (!irrg_close_topmost_report_or_help() && irrg_city_dialog_is_open())
          irrg_city_dialog_close();
        return true;
      case irr::KEY_KEY_R: if (press) irrg_reports_open(0); break; /* units */
      case irr::KEY_KEY_O: if (press) irrg_reports_open(2); break; /* economy */
      case irr::KEY_KEY_S: if (press) irrg_reports_open(1); break; /* science */
      case irr::KEY_KEY_H: if (press) irrg_help_open();           break;
      case irr::KEY_KEY_F:
        /* Toggle the fog of war (FreeCiv's standard option). OFF reveals the
         * terrain of tiles you have EXPLORED but can no longer see. (Tiles you
         * have never seen stay dark: the client has no data for them -- move
         * units around to explore.) */
        if (press && client_state() == C_S_RUNNING)
          request_toggle_fog_of_war();
        break;
      case irr::KEY_KEY_G:
        /* Open the in-game graphics settings menu (zoom / fog / 3D). */
        if (press && client_state() == C_S_RUNNING)
          irrg_open_settings();
        break;
      case irr::KEY_KEY_L:
        /* Toggle the "Messages" submenu (the game log, as a panel). */
        if (press && client_state() == C_S_RUNNING) {
          if (irrg_log_is_open()) irrg_log_close();
          else                    irrg_log_open();
        }
        break;
      default: break;
      }
      return false;
    }
    if (event.EventType != irr::EET_MOUSE_INPUT_EVENT)
      return false;
    const irr::SEvent::SMouseInput &m = event.MouseInput;
    switch (m.Event) {
    case irr::EMIE_MOUSE_MOVED:
      irrg_unitbar_mouse_move(m.X, m.Y);   /* hover highlight for the bar */
      irrg_endturn_mouse_move(m.X, m.Y);   /* hover highlight for End Turn */
      irrg_menu_button_mouse_move(m.X, m.Y); /* hover highlight for the Menu button */
      if (g_panning) {
        irrg_pan_step(m.X, m.Y);           /* middle- or left-drag panning */
      } else if (g_left_down && irrg_map3d_is_built()) {
        int ddx = m.X - g_left_start_x; if (ddx < 0) ddx = -ddx;
        int ddy = m.Y - g_left_start_y; if (ddy < 0) ddy = -ddy;
        if (ddx > kPanDragThreshold || ddy > kPanDragThreshold) {
          g_left_dragging = true;
          irrg_pan_begin(m.X, m.Y);        /* a left press that moved -> a pan */
        }
      }
      break;
    case irr::EMIE_LMOUSE_PRESSED_DOWN:
      irrg_unitbar_mouse_down(m.X, m.Y);   /* "pressed" look for the bar */
      g_left_down = true; g_left_dragging = false;
      g_left_start_x = m.X; g_left_start_y = m.Y;
      break;
    case irr::EMIE_LMOUSE_LEFT_UP:
      {
        bool was_drag = g_left_dragging;
        g_left_down = false; g_left_dragging = false; g_panning = false;
        if (was_drag) {                    /* it was a map pan, not a click */
          irrg_unitbar_release();          /* drop the "pressed" look */
          break;
        }
        if (irrg_unitbar_handle_click(m.X, m.Y))
          break;   /* a unit action button was clicked */
        irrg_handle_map_click(m.X, m.Y, true);
      }
      break;
    case irr::EMIE_RMOUSE_LEFT_UP:
      irrg_handle_map_click(m.X, m.Y, false);
      break;
    case irr::EMIE_MMOUSE_PRESSED_DOWN:
      if (irrg_map3d_is_built()) irrg_pan_begin(m.X, m.Y);   /* grab-pan */
      break;
    case irr::EMIE_MMOUSE_LEFT_UP:
      g_panning = false;
      break;
    case irr::EMIE_MOUSE_WHEEL:
      if (irrg_map3d_is_built())
        irrg_map3d_zoom(m.Wheel > 0 ? 1.1f : 0.9f);
      break;
    default:
      break;
    }
    return false;
  }
};

static irrg_event_receiver g_receiver;

/* Register the mouse event receiver with the device (call once after the
 * device is created, before the event loop). */
void irrg_setup_input(void)
{
  irr::IrrlichtDevice *dev = (irr::IrrlichtDevice *)irrg_device();
  if (dev)
    dev->setEventReceiver(&g_receiver);
}

/* Find one of our units (by id) and return its current map position. */
bool irrg_unit_pos(int unit_id, int *out_x, int *out_y)
{
  const struct player *me = client_player();
  if (!me)
    return false;
  unit_list_iterate(me->units, punit) {
    if (punit->id == unit_id && punit->tile) {
      if (out_x) *out_x = index_to_map_pos_x(tile_index(punit->tile));
      if (out_y) *out_y = index_to_map_pos_y(tile_index(punit->tile));
      return true;
    }
  } unit_list_iterate_end;
  return false;
}

/* FC_IRR_DIAGUNIT: one-shot (headless) log of each of our cities/units with
 * their MAP position and CANVAS position (at map_zoom). Lets us verify they
 * land on the visible map and correlate canvas coords with a pixel dump to
 * check that the city/unit art is actually being drawn. */
void irrg_diag_units_cities(void)
{
  const struct player *me = client_player();
  if (!me) { irrg_log("DIAGUC: no player"); return; }
  { char hdr[80];
    std::snprintf(hdr, sizeof(hdr), "DIAGUC: === cities/units (map_zoom=%.2f) ===",
                  (double)map_zoom);
    irrg_log(hdr); }
  int nc = 0;
  city_list_iterate(me->cities, pcity) {
    float cx = -1, cy = -1;
    bool ok = (pcity->tile && tile_to_canvas_pos(&cx, &cy, map_zoom, pcity->tile));
    int mx = pcity->tile ? index_to_map_pos_x(tile_index(pcity->tile)) : -1;
    int my = pcity->tile ? index_to_map_pos_y(tile_index(pcity->tile)) : -1;
    char b[190];
    if (ok)
      std::snprintf(b, sizeof(b), "DIAGUC city '%s' id=%d map(%d,%d) canvas(%d,%d)",
                    pcity->name ? pcity->name : "?", pcity->id, mx, my, (int)cx, (int)cy);
    else
      std::snprintf(b, sizeof(b), "DIAGUC city '%s' id=%d map(%d,%d) canvas=OFFSCREEN",
                    pcity->name ? pcity->name : "?", pcity->id, mx, my);
    irrg_log(b);
    nc++;
  } city_list_iterate_end;
  irrg_log((std::string("DIAGUC: cities=") + std::to_string(nc)).c_str());
  int nu = 0;
  unit_list_iterate(me->units, punit) {
    if (!punit->tile) { nu++; continue; }
    float cx = -1, cy = -1;
    bool ok = tile_to_canvas_pos(&cx, &cy, map_zoom, punit->tile);
    int mx = index_to_map_pos_x(tile_index(punit->tile));
    int my = index_to_map_pos_y(tile_index(punit->tile));
    char b[190];
    if (ok)
      std::snprintf(b, sizeof(b), "DIAGUC unit id=%d map(%d,%d) canvas(%d,%d)",
                    punit->id, mx, my, (int)cx, (int)cy);
    else
      std::snprintf(b, sizeof(b), "DIAGUC unit id=%d map(%d,%d) canvas=OFFSCREEN",
                    punit->id, mx, my);
    irrg_log(b);
    nu++;
  } unit_list_iterate_end;
  irrg_log((std::string("DIAGUC: units=") + std::to_string(nu)).c_str());
}

/* Headless diagnostic: draw a small bright box on the map store at each of our
 * unit tiles (red) and city tiles (cyan). Lets a dump show unambiguously where
 * units/cities land relative to the visible area (and whether a sprite is under
 * the box). Gated by FC_IRR_MARKUNITS. */
void irrg_mark_units(struct canvas *cv)
{
  if (!cv) return;
  const struct player *me = client_player();
  if (!me) return;
  static struct color red  = { 255,  30,  30 };
  static struct color cyan = {  30, 235, 235 };
  unit_list_iterate(me->units, punit) {
    if (!punit->tile) continue;
    float cx = -1, cy = -1;
    if (tile_to_canvas_pos(&cx, &cy, map_zoom, punit->tile))
      irrg_canvas_put_rectangle(cv, &red, (int)cx - 2, (int)cy - 2, 5, 5);
  } unit_list_iterate_end;
  city_list_iterate(me->cities, pcity) {
    if (!pcity->tile) continue;
    float cx = -1, cy = -1;
    if (tile_to_canvas_pos(&cx, &cy, map_zoom, pcity->tile))
      irrg_canvas_put_rectangle(cv, &cyan, (int)cx - 4, (int)cy - 4, 9, 9);
  } city_list_iterate_end;
}

/* Headless test: create a city on our first unit's tile via the map editor
 * (requires the client to hold editor/hack rights, which the test server grants).
 * Returns TRUE if a create packet was sent. */
bool irrg_spawn_test_city(void)
{
  /* Found a city with one of our settlers (the normal game action -- no editor
   * rights needed). Picks the first unit that can do ACTION_FOUND_CITY and
   * issues the action on its own tile. */
  const struct player *me = client_player();
  if (!me) { irrg_log("DIAGUC: spawncity: no player"); return false; }
  unit_list_iterate(me->units, punit) {
    if (punit->tile && unit_can_do_action(punit, ACTION_FOUND_CITY)) {
      int t = tile_index(punit->tile);
      irrg_log((std::string("DIAGUC: spawncity: settler unit ")
                + std::to_string(punit->id)
                + " founds city on tile " + std::to_string(t)).c_str());
      request_do_action(ACTION_FOUND_CITY, punit->id, t, 0, "");
      return true;
    }
  } unit_list_iterate_end;
  irrg_log("DIAGUC: spawncity: no unit can found a city (no settler?)");
  return false;
}

/* Headless unit-move verification: select one of our movable units and issue a
 * goto to an adjacent walkable tile. */
bool irrg_simmove(int *out_unit_id, int *out_from_x, int *out_from_y,
                  int *out_to_x, int *out_to_y)
{
  const struct player *me = client_player();
  if (!me)
    return false;

  /* Find one of our movable units (has movement left + a tile). */
  struct unit *punit = NULL;
  unit_list_iterate(me->units, u) {
    if (u->moves_left > 0 && u->tile) { punit = u; break; }
  } unit_list_iterate_end;
  if (!punit) {
    irrg_log("simmove: no movable unit found.");
    return false;
  }

  const int fx = index_to_map_pos_x(tile_index(punit->tile));
  const int fy = index_to_map_pos_y(tile_index(punit->tile));

  /* Find an adjacent walkable tile (N/E/S/W). */
  static const int dx[4] = { 0, 1, 0, -1 };
  static const int dy[4] = { -1, 0, 1, 0 };
  struct tile *dest = NULL;
  for (int i = 0; i < 4 && !dest; ++i) {
    struct tile *nt = map_pos_to_tile(&wld.map, fx + dx[i], fy + dy[i]);
    if (nt && unit_can_move_to_tile(&wld.map, punit, nt,
                                    /*igzoc=*/false,
                                    /*enter_transport=*/false,
                                    /*enter_enemy_city=*/false))
      dest = nt;
  }
  if (!dest) {
    std::string msg = std::string("simmove: unit ") + std::to_string(punit->id) +
                      " at (" + std::to_string(fx) + "," + std::to_string(fy) +
                      ") has no walkable neighbor.";
    irrg_log(msg.c_str());
    return false;
  }

  /* Select + goto (the same core calls a real mouse-driven move uses). */
  unit_focus_set_and_select(punit);
  request_unit_goto(ORDER_LAST, ACTION_NONE, -1);
  do_unit_goto(dest);
  clear_hover_state();

  if (out_unit_id) *out_unit_id = punit->id;
  if (out_from_x) *out_from_x = fx;
  if (out_from_y) *out_from_y = fy;
  if (out_to_x) *out_to_x = index_to_map_pos_x(tile_index(dest));
  if (out_to_y) *out_to_y = index_to_map_pos_y(tile_index(dest));
  return true;
}
