/***********************************************************************
 Freeciv - gui-irrlicht main entry + device + event loop.
   Phase 3: real main() -> client_main(); device creation (GL -> SW -> NULL
   fallback); a manual event loop that pumps Irrlicht events (device->run()
   is non-blocking on X11), selects on the server socket, runs idle
   callbacks (one per frame, like gui-sdl2), and renders.
***********************************************************************/
#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>      /* usleep */
#include <sys/select.h>  /* fd_set */

#include <irrlicht.h>

#include "client_main.h"   /* client_main() */
#include "clinet.h"        /* input_from_server() */
#include "netintf.h"       /* fc_select(), fc_timeval */
#include "chatline_common.h" /* send_chat() for auto-start */
#include "mapview_common.h"  /* map_canvas_resized() */
#include "options.h"         /* gui_options.draw_fog_of_war (fog toggle) */
#include "control.h"         /* request_toggle_fog_of_war() */
#include "climisc.h"         /* center_on_something() */
#include "irrg_cxxside.h"
#include "graphics.h"
#include "irrg_map3d.h"   /* Phase 5: 3D map view */
#include "irrg_interact.h" /* Phase 6: interactive map controls + simmove */
#include "irrg_dialogs.h"  /* Phase 6: message window + city dialog */
#include "irrg_menu.h"     /* pre-game main menu (connect/quit) */
#include "irrg_settings.h"  /* in-game graphics settings menu */
#include "irrg_newgame.h"   /* new-game options screen */
#include "irrg_gamemenu.h"  /* in-game menu (GUI path to every action) */
#include "player.h"        /* player_primary_capital (CITYDLG test) */
#include "city.h"          /* struct city, city_name_get (CITYDLG test) */
#include "world_object.h"  /* wld.map (loading progress: map dims + tile terrain) */
#include "map.h"           /* map_pos_to_tile() */
#include "tile.h"          /* tile_terrain() */

using namespace irr;

/* Device / subsystem singletons (raw pointers; owned for the process life). */
static irr::IrrlichtDevice *g_device = 0;
static scene::ISceneManager *g_scmgr = 0;
static video::IVideoDriver *g_vdriver = 0;
static gui::IGUIEnvironment *g_guienv = 0;

void *irrg_device(void)  { return g_device; }
void *irrg_scmgr(void)   { return g_scmgr; }
void *irrg_vdriver(void) { return g_vdriver; }
void *irrg_guienv(void)  { return g_guienv; }

static void irrg_log(const char *msg)
{
  std::fprintf(stderr, "[irrg] %s\n", msg);
  std::fflush(stderr);
}

/* ---- Event-loop state ---- */
static int net_socket = -1;
struct irrg_idle {
  void (*cb)(void *);
  void *data;
  struct irrg_idle *next;
};
static struct irrg_idle *idle_head = 0;

/* Phase 6 headless unit-move verification state (FC_IRR_SIMMOVE=1). */
static int   g_sim_id = -1, g_sim_fx = 0, g_sim_fy = 0, g_sim_tx = 0, g_sim_ty = 0;
static long  g_sim_frame = -1;
static bool  g_sim_done = false;
static bool  g_citydlg_done = false;   /* FC_IRR_CITYDLG test */
static bool  g_report_done  = false;   /* FC_IRR_REPORT test */

/* ---- Runtime graphics state (edited by the in-game Settings menu) ---- */
static float g_map_zoom_present = 2.0f;  /* 2D presentation zoom, 1.0-4.0 */
static bool  g_use3d          = false;  /* 3D view on (built lazily) */
static bool  g_settings_open  = false;  /* graphics settings modal is up */
static bool  g_newgame_open   = false;  /* new-game options modal is up */
static bool  g_gamemenu_open  = false;  /* in-game menu modal is up */
static bool  irrg_gamemenu_dump = false; /* one-shot: dump the menu canvas (test) */
/* PREPARING-screen button rects (filled each frame in the render branch); used
 * by irrg_prep_button_hit so a mouse click can start a game / disconnect. */
static int g_prep_start_x=0, g_prep_start_y=0, g_prep_start_w=0, g_prep_start_h=0;
static int g_prep_disc_x =0, g_prep_disc_y =0, g_prep_disc_w =0, g_prep_disc_h =0;

double irrg_get_map_zoom_present(void) { return (double)g_map_zoom_present; }
void irrg_set_map_zoom_present(double z)
{
  if (z < 1.0) z = 1.0;
  if (z > 4.0) z = 4.0;
  g_map_zoom_present = (float)z;
}
bool irrg_fog_enabled(void) { return (bool)gui_options.draw_fog_of_war; }
void irrg_set_fog_enabled(bool on)
{
  if ((bool)gui_options.draw_fog_of_war != on)
    request_toggle_fog_of_war();   /* toggles to the requested value */
}
bool irrg_use3d(void) { return g_use3d; }
void irrg_set_use3d(bool on)
{
  if (on && !irrg_map3d_is_built()) {
    irrg_log("building 3D map on demand (Settings > 3D Terrain = On)");
    if (!irrg_map3d_build()) { irrg_log("3D build failed; staying in 2D"); return; }
  }
  g_use3d = on;
}
void irrg_open_settings(void) { g_settings_open = true; }
void irrg_open_newgame(void)  { g_newgame_open  = true; }
void irrg_open_gamemenu(void) { g_gamemenu_open = true; }

/* Hit-test the two PREPARING-screen buttons (the mouse equivalents of the
 * ENTER/ESC shortcuts). 0 = "Start a New Game", 1 = "Disconnect", -1 = none. */
int irrg_prep_button_hit(int x, int y)
{
  if (g_prep_start_w > 0 && x >= g_prep_start_x && x < g_prep_start_x + g_prep_start_w
      && y >= g_prep_start_y && y < g_prep_start_y + g_prep_start_h) return 0;
  if (g_prep_disc_w > 0 && x >= g_prep_disc_x && x < g_prep_disc_x + g_prep_disc_w
      && y >= g_prep_disc_y && y < g_prep_disc_y + g_prep_disc_h) return 1;
  return -1;
}

/* ---- Lifecycle ---- */
void irrg_ui_init(void)
{
  irrg_log("ui_init: gui-irrlicht (Phase 3).");
}

int irrg_ui_main(int argc, char *argv[])
{
  (void)argc; (void)argv;
  irrg_log("ui_main: creating device (GL -> SW -> NULL)...");

  /* The default (forced) square tileset, "Trident", ships without the "full
   * citybar" sprites (citybar.occupancy_N etc.). show_full_citybar() dereferences
   * citybar->occupancy.p[CLIP(0,count,size-1)] and, with an empty array,
   * computes an index of -1 -> out-of-bounds read -> SIGSEGV the moment a city
   * appears on the map. Force the safe text-based city label instead (the
   * option default is TRUE). This is applied before the first map paint. */
  gui_options.draw_full_citybar = FALSE;
  irrg_log("ui_main: draw_full_citybar disabled (square tileset has no full citybar)");

  /* Found City: the server answers city_name_suggestion_req by sending a
   * suggested name; the client then either founds the city directly
   * (ask_city_name == FALSE) or pops up a naming dialog (popup_newcity_dialog).
   * This client has no such dialog, so with the default (TRUE) the found-city
   * request silently never completes. Force it FALSE so the Found City button
   * founds the city immediately using the server-suggested name. */
  gui_options.ask_city_name = FALSE;
  irrg_log("ui_main: ask_city_name disabled (no city-naming dialog; found directly)");

  /* FC_IRR_DRIVER=opengl|software|null forces a driver (handy for headless). */
  const char *force = std::getenv("FC_IRR_DRIVER");
  video::E_DRIVER_TYPE types[3] = { video::EDT_OPENGL, video::EDT_SOFTWARE,
                                    video::EDT_NULL };
  const char *names[3] = { "OpenGL", "Software", "Null" };
  int ntry = 3;
  if (force) {
    if (!std::strcmp(force, "opengl"))       { types[0] = video::EDT_OPENGL;   names[0] = "OpenGL";   ntry = 1; }
    else if (!std::strcmp(force, "software")){ types[0] = video::EDT_SOFTWARE; names[0] = "Software"; ntry = 1; }
    else if (!std::strcmp(force, "null"))    { types[0] = video::EDT_NULL;     names[0] = "Null";     ntry = 1; }
  }
  for (int i = 0; i < ntry && !g_device; ++i) {
    SIrrlichtCreationParameters p;
    p.DriverType = types[i];
    p.WindowSize = core::dimension2d<u32>(1280, 800);
    p.Bits = 32;
    p.Fullscreen = false;
    p.Stencilbuffer = false;
    p.Vsync = true;
    /* The software driver logs a "slow unlock of non power of 2 texture"
     * warning on every present (our canvas isn't power-of-2); silence the
     * noise for a headless client. */
    p.LoggingLevel = irr::ELL_ERROR;
    g_device = createDeviceEx(p);
    irrg_log((std::string(g_device ? "device created: " : "device FAILED: ")
              + names[i]).c_str());
  }
  if (!g_device) {
    irrg_log("no video driver available; continuing headless (no window).");
    return 0;
  }
  g_scmgr = g_device->getSceneManager();
  g_vdriver = g_device->getVideoDriver();
  g_guienv = g_device->getGUIEnvironment();

  /* Phase 6: register the mouse event receiver (left=select, right=move/
   * recenter, wheel=zoom). No-op headlessly (no mouse), but makes the client
   * interactive on a real display. */
  if (std::getenv("FC_IRR_NOINPUT") == 0) irrg_setup_input();

  /* Kick the state machine: with --autoconnect this triggers
   * start_autoconnecting_to_server() (oldstate C_S_INITIAL -> DISCONNECTED). */
  set_client_state(C_S_DISCONNECTED);

  irrg_log("ui_main: entering event loop.");
  const bool auto_start = (std::getenv("FC_IRR_AUTOSTART") != 0);
  if (auto_start) irrg_log("auto-start enabled (FC_IRR_AUTOSTART).");
  /* FC_IRR_AUTOTURN=1: automatically end the player's turn every ~60 frames
   * (headless play-through). OFF by default so a real player is never cut off
   * mid-turn -- in interactive play the turn is ended only via the in-game
   * menu's "End Turn" action. */
  const bool auto_turn = (std::getenv("FC_IRR_AUTOTURN") != 0);
  if (auto_turn) irrg_log("auto-end-turn enabled (FC_IRR_AUTOTURN); otherwise use Menu > End Turn.");
  /* FC_IRR_MAP3D: 3D map view is now the DEFAULT; set FC_IRR_MAP3D=0 to force
   * the classic 2D tile view (useful as a fallback). 1 explicitly enables 3D. */
  const char *m3 = std::getenv("FC_IRR_MAP3D");
  const bool map3d = (m3 != 0) ? (m3[0] != '0') : true;
  g_use3d = map3d;   /* default 3D; FC_IRR_MAP3D=0 -> 2D; Settings can override */
  if (map3d) irrg_log("3D map view (default). Set FC_IRR_MAP3D=0 for the 2D view.");
  else irrg_log("2D map view (FC_IRR_MAP3D=0).");
  const bool simmove_enabled = (std::getenv("FC_IRR_SIMMOVE") != 0);
  if (simmove_enabled) irrg_log("unit-move verification enabled (FC_IRR_SIMMOVE).");
  const bool citydlg_enabled = (std::getenv("FC_IRR_CITYDLG") != 0);
  if (citydlg_enabled) irrg_log("city-dialog test enabled (FC_IRR_CITYDLG).");
  /* FC_IRR_REPORT=<0=units|1=science|2=economy|3=help> opens that dialog once
   * the game is running (headless can't press R/O/S/H). */
  const char *rep_env = std::getenv("FC_IRR_REPORT");
  const int   report_test = rep_env ? std::atoi(rep_env) : -1;
  if (report_test >= 0) irrg_log((std::string("report test enabled (FC_IRR_REPORT=") + rep_env + ").").c_str());
  bool map_initialized = false;
  long frames_since_init = 0;   /* frames in C_S_RUNNING since the map was set up */
  /* Main menu: shown before a connection is established (unless the user asked
   * to autoconnect, which skips it). "Connect" triggers the same autoconnect
   * path the headless tests use (start_autoconnecting_to_server + the per-frame
   * try_to_autoconnect() below). */
  const bool want_autoc = auto_connect;
  bool connect_initiated = false;
  bool connect_observer = false;   /* menu "Connect as observer" was chosen */
  bool was_observer = false;       /* tracks client_is_global_observer() transitions */
  /* Presentation zoom for the 2D map (see FC_IRR_ZOOM). We do NOT change
   * FreeCiv's mapview zoom (that fights the map-centering); instead we present
   * a centered, upscaled crop of the map canvas so the small explored area
   * fills more of the screen. 1.0 == 1:1 (unchanged). */
  /* Seed the (file-scope) presentation zoom from FC_IRR_ZOOM; the Settings
   * menu edits the same value via irrg_set_map_zoom_present(). */
  {
    const char *zenv = std::getenv("FC_IRR_ZOOM");
    if (zenv) g_map_zoom_present = std::atof(zenv);
    if (g_map_zoom_present < 1.0f) g_map_zoom_present = 1.0f;
    if (g_map_zoom_present > 4.0f) g_map_zoom_present = 4.0f;
  }
  if (g_map_zoom_present > 1.0001f)
    irrg_log((std::string("2D map presentation zoom = ") + std::to_string(g_map_zoom_present) + " (FC_IRR_ZOOM)").c_str());
  long frames = 0;
  while (g_device && g_device->run()) {
    /* FC_IRR_VERB=1: per-frame state trace (debug frame rate + observer state). */
    if (std::getenv("FC_IRR_VERB")) {
      char b[200];
      std::snprintf(b, sizeof(b),
                    "verb: f=%ld st=%d est=%d obs=%d play=%p id=%d",
                    frames, (int)client_state(),
                    (int)client.conn.established, (int)client.conn.observer,
                    (void *)client.conn.playing, client.conn.id);
      irrg_log(b);
    }
    /* 0. Drive autoconnect (no-op unless the core is autoconnecting). */
    if ((frames % 5) == 0) {
      try_to_autoconnect();
    }

    /* 0b. Headless turn-advance (FC_IRR_AUTOTURN=1 only): once in the game
     * (C_S_RUNNING) and it is our phase, end the turn (send_turn_done() no-ops
     * unless can_end_turn()). Lets a headless client play through turns without
     * a UI. OFF by default -- an interactive player ends their turn via the
     * in-game menu's "End Turn" action. Disabled while a unit-move verification
     * is in flight (ending the turn would reset it). */
    if (auto_turn && client_state() == C_S_RUNNING && !simmove_enabled
        && (frames % 60) == 0) {
      user_ended_turn();
    }

    /* 0c. Auto-start (FC_IRR_AUTOSTART=1): the server only begins the game
     * once every connected player is ready, so send "/start" (marks us ready)
     * while in PREPARING. No-op once RUNNING. Lets a headless client reach
     * C_S_RUNNING so the real map view renders. */
    if (auto_start && client_state() == C_S_PREPARING
        && net_socket >= 0 && (frames % 60) == 0) {
      send_chat("/start");
    }

    /* 0d. Map-view init: a headless client has no window-resize event to
     * create the map canvas (map_canvas_resized), so do it ourselves once we
     * are RUNNING, then center+redraw the full map into it. If the 3D view is
     * enabled, also build the merged terrain mesh from the real map. */
    if (client_state() == C_S_RUNNING && !map_initialized) {
      /* FC_IRR_NOFOG=1: start with the fog of war OFF (FreeCiv's
       * gui_options.draw_fog_of_war) so explored-but-no-longer-visible tiles
       * show their terrain instead of the fog overlay. Set before the first
       * draw so it applies to the initial render. Press F in-game to toggle.
       * (Never-seen tiles stay dark regardless -- the client has no data.) */
      const char *noFog = std::getenv("FC_IRR_NOFOG");
      if (noFog && noFog[0] != '0') {
        gui_options.draw_fog_of_war = FALSE;
        irrg_log("fog of war off at startup (FC_IRR_NOFOG); press F to toggle.");
      }
      map_canvas_resized(1280, 800);
      center_on_something();
      map_initialized = true;
      frames_since_init = 0;
      was_observer = client_is_global_observer();
      irrg_log((std::string("map canvas initialized (C_S_RUNNING); observer=") +
                (was_observer ? "yes (full map)" : "no") +
                (connect_observer ? " [observer requested; grant on server: observe <name>]" : "") +
                ((client_player() != 0 && !was_observer)
                 ? " [view focused on player units/capital]" : "") + ".").c_str());
      if (std::getenv("FC_IRR_DIAG")) {          /* FC_IRR_DIAG=1: map stats */
        extern void irrg_diag_map(void);
        irrg_diag_map();
      }
      /* FC_IRR_TESTLOAD=1: skip the 3D build so the "Loading map" progress
       * screen stays up (is_built() false) for a headless screenshot.
       * FC_IRR_TESTLOAD_RETRY=1: skip the INIT build but let the per-frame
       * RETRY (step 0d2) build it -- reproduces the 'terrain still downloading
       * at init' case to verify the loading screen then resolves (no hang). */
      if (map3d && !irrg_map3d_is_built()
          && !std::getenv("FC_IRR_TESTLOAD") && !std::getenv("FC_IRR_TESTLOAD_RETRY")) {
        /* The 3D build iterates the whole map + copies meshes; give the map a
         * frame or two to settle, then build once. */
        if (irrg_map3d_build()) {
          irrg_log("3D map built.");
          /* Render + dump + pick immediately (the software 3D rasterizer is slow
           * enough that waiting for a periodic frame would take a long time). */
          irrg_map3d_render();
          const char *dump3d = std::getenv("FC_IRR_DUMP3D");
          if (dump3d && g_vdriver) {
            video::IImage *img = g_vdriver->createScreenShot();
            if (img) {
              u32 W = img->getDimension().Width, H = img->getDimension().Height;
              std::FILE *f = std::fopen(dump3d, "wb");
              if (f) {
                std::fprintf(f, "P6\n%d %d\n255\n", W, H);
                for (u32 y = 0; y < H; ++y)
                  for (u32 x = 0; x < W; ++x) {
                    video::SColor c = img->getPixel(x, y);
                    unsigned char rgb[3] = { c.getRed(), c.getGreen(), c.getBlue() };
                    std::fwrite(rgb, 1, 3, f);
                  }
                std::fclose(f);
                irrg_log((std::string("dumped 3D map ") + dump3d +
                          " (" + std::to_string(W) + "x" + std::to_string(H) + ")").c_str());
              }
              img->drop();
            }
          }
          const char *pick = std::getenv("FC_IRR_PICK");
          if (pick && g_vdriver) {
            int px = 0, py = 0;
            core::dimension2d<u32> sz = g_vdriver->getScreenSize();
            if (irrg_map3d_pick((int)(sz.Width / 2), (int)(sz.Height / 2), &px, &py))
              irrg_log((std::string("pick@center -> map tile (") +
                        std::to_string(px) + "," + std::to_string(py) + ")").c_str());
            else
              irrg_log("pick@center -> no hit");
          }
        }
      }
    }

    /* 0d2. Observer transition + periodic grey-map self-diagnosis (2D only).
     * When the server makes us a global observer the full map arrives; re-center
     * (observers have no player -> map center). While running, periodically log
     * whether the presented crop is dark (empty store = no data / fog; dark crop
     * but bright store = the view is not centered on the explored area). */
    /* Runs in both the 2D and 3D views: observer recenter + the FC_IRR_* test
     * hooks (SPAWNCITY / CITYDLG / DIAGUNIT). mapview.store is maintained in
     * 3D too (map_canvas_resized + per-frame unqueue), so these are safe. */
    if (client_state() == C_S_RUNNING && map_initialized) {
      /* 3D build RETRY: the init-time build (step 0d) is attempted only once,
       * and can fail if the map terrain hasn't finished arriving yet (the
       * explored area is still downloading from the server). That would leave
       * is_built() false forever -> the "Loading map" screen hangs. So retry
       * each frame until the terrain is present and the mesh builds (the heavy
       * build runs once; failed attempts are a cheap O(map) terrain scan). */
      if (map3d && !irrg_map3d_is_built() && !std::getenv("FC_IRR_TESTLOAD")) {
        if (irrg_map3d_build())
          irrg_log("3D map built (retry, after map terrain arrived).");
      }
      bool is_obs = client_is_global_observer();
      if (is_obs != was_observer) {
        irrg_log(is_obs
                 ? "observer state -> GLOBAL OBSERVER: full map received, re-centering on map center"
                 : "observer state -> player: re-centering on player units/capital");
        center_on_something();
        was_observer = is_obs;
      }
      if (++frames_since_init == 20 || (frames_since_init % 300) == 0)
        irrg_diagnose_present(mapview.store, g_map_zoom_present);
      /* FC_IRR_DIAGUNIT=1: once the map has settled, log our cities/units with
       * their map + canvas positions (verify they're on the visible map). */
      if (std::getenv("FC_IRR_DIAGUNIT")) {
        static bool g_diagunit_done = false;
        if (!g_diagunit_done && frames_since_init >= 20) {
          g_diagunit_done = true;
          irrg_diag_units_cities();
        }
      }
      /* FC_IRR_SPAWNCITY=1: create a test city on our unit tile (map editor,
       * needs editor/hack rights) to verify city rendering + the city dialog.
       * Spawn once at frame 20, then re-log at frame 60 to confirm it landed. */
      if (std::getenv("FC_IRR_SPAWNCITY")) {
        static int g_spawn_state = 0;
        if (g_spawn_state == 0 && frames_since_init >= 20) {
          irrg_spawn_test_city();
          g_spawn_state = 1;
        }
        if (g_spawn_state == 1 && frames_since_init >= 60) {
          g_spawn_state = 2;
          irrg_diag_units_cities();
        }
      }
      /* FC_IRR_CITYDLG=1: once we have a city, open its control dialog (verify
       * the production menu renders). FC_IRR_CITYBUILD=<n> additionally selects
       * buildable #n and starts production on it (verify city_change_production
       * reaches the server and the city's production changes). */
      if (std::getenv("FC_IRR_CITYDLG")) {
        static int g_cd_state = 0;
        static struct city *g_cd_city = nullptr;
        static char g_cd_before[64] = "";
        if (g_cd_state == 0 && frames_since_init >= 90) {
          const struct player *me = client_player();
          struct city *c = nullptr;
          if (me) { city_list_iterate(me->cities, pc) { c = pc; break; } city_list_iterate_end; }
          if (c) {
            irrg_city_dialog_open(c);
            g_cd_city = c;
            universal_name_translation(&c->production, g_cd_before, sizeof(g_cd_before));
            char lb[128];
            std::snprintf(lb, sizeof(lb), "CITYDLG: dialog open, production before='%s'", g_cd_before);
            irrg_log(lb);
            const char *cb = std::getenv("FC_IRR_CITYBUILD");
            if (cb) irrg_city_dialog_test_build(std::atoi(cb));
            g_cd_state = 1;
          }
        }
        if (g_cd_state == 1 && frames_since_init >= 160 && g_cd_city) {
          char after[64], la[160];
          universal_name_translation(&g_cd_city->production, after, sizeof(after));
          bool changed = (std::strcmp(after, g_cd_before) != 0);
          std::snprintf(la, sizeof(la),
                        "CITYDLG: production after='%s' (before='%s') CHANGED=%s",
                        after, g_cd_before, changed ? "YES" : "no");
          irrg_log(la);
          g_cd_state = 2;
        }
      }
    }

    /* 0e. Phase 6 headless unit-move verification (FC_IRR_SIMMOVE=1): once in
     * the game, select one of our movable units and issue a goto to an adjacent
     * tile, then (a few frames later, once the server has applied it) report
     * whether the unit actually moved. */
    if (simmove_enabled && client_state() == C_S_RUNNING && map_initialized
        && !g_sim_done) {
      if (g_sim_id < 0 && (frames % 30) == 0) {
        if (irrg_simmove(&g_sim_id, &g_sim_fx, &g_sim_fy, &g_sim_tx, &g_sim_ty)) {
          std::string msg = std::string("simmove: unit ") + std::to_string(g_sim_id) +
                            " from (" + std::to_string(g_sim_fx) + "," + std::to_string(g_sim_fy) +
                            ") -> (" + std::to_string(g_sim_tx) + "," + std::to_string(g_sim_ty) +
                            ") [move order sent]";
          irrg_log(msg.c_str());
          g_sim_frame = frames;
        }
      } else if (g_sim_id >= 0 && g_sim_frame >= 0 && frames - g_sim_frame >= 40) {
        int cx = 0, cy = 0;
        if (irrg_unit_pos(g_sim_id, &cx, &cy)) {
          bool moved = (cx != g_sim_fx || cy != g_sim_fy);
          std::string msg = std::string("simmove: unit ") + std::to_string(g_sim_id) +
                            " now at (" + std::to_string(cx) + "," + std::to_string(cy) +
                            ") moved=" + (moved ? "YES" : "no");
          irrg_log(msg.c_str());
        } else {
          irrg_log("simmove: unit no longer found (moved out of view / lost?).");
        }
        g_sim_done = true;
      }
    }

    /* (The old FC_IRR_CITYDLG hook that opened a NULL-owner dummy_city was
     * removed: it crashed build_prod_list in can_city_build (NULL owner).
     * FC_IRR_CITYDLG is now handled above by the hook that opens the real
     * player's city.)

    /* 0g. FC_IRR_REPORT test: open the chosen report/help dialog once running. */
    if (report_test >= 0 && client_state() == C_S_RUNNING && !g_report_done) {
      if (report_test == 3)      irrg_help_open();
      else                        irrg_reports_open(report_test & 3);
      g_report_done = true;
      irrg_log("REPORT test: opened dialog.");
    }

    /* 1. Network input (paced by a short select). */
    if (net_socket >= 0) {
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(net_socket, &fds);
      struct timeval tv = { 0, 5000 };   /* 5 ms */
      int r = fc_select(net_socket + 1, &fds, 0, 0, &tv);
      if (r > 0 && FD_ISSET(net_socket, &fds))
        input_from_server(net_socket);
    } else {
      usleep(10000);   /* 10 ms when not connected */
    }

    /* 1b. Track + log connection / state transitions (diagnostic). */
    {
      static int last_netsock = -999, last_state = -1;
      int ns = net_socket;
      int st = (int)client_state();
      if (ns != last_netsock) { irrg_log((std::string("netsock: ")+std::to_string(last_netsock)+" -> "+std::to_string(ns)).c_str()); last_netsock = ns; }
      if (st != last_state) { irrg_log((std::string("state: ")+std::to_string(last_state)+" -> "+std::to_string(st)).c_str()); last_state = st; }
    }

    /* 2. One idle callback per frame (mirrors gui-sdl2). */
    if (idle_head) {
      struct irrg_idle *c = idle_head;
      idle_head = c->next;
      c->cb(c->data);
      std::free(c);
    }

    /* 2b. Drain the queued mapview updates into mapview.store. FreeCiv's
     *     packet handlers call refresh_tile/unit/city_mapcanvas(..., FALSE),
     *     which only QUEUES the change; a GUI is expected to process the queue
     *     each frame (gui-sdl2/gtk do this in their draw callback) before
     *     presenting the map canvas. Without this the map is drawn once by the
     *     initial center_on_something() and then never updated -> a flat grey
     *     (COLOR_MAPVIEW_UNKNOWN) screen. Cheap no-op when the queue is empty. */
    unqueue_mapview_updates(FALSE);

    /* 2c. Main menu: shown before a connection (unless autoconnecting, which
     *     skips it). Poll pending menu actions, decide whether to show it, and
     *     sync its state. A failed connect drops us back to C_S_DISCONNECTED,
     *     which re-arms the menu so the user can retry. */
    if (irrg_menu_take_connect()) {
      connect_initiated = true;
      connect_observer = false;
      irrg_log((std::string("connecting to ") + server_host + ":" +
                std::to_string(server_port) + " as player").c_str());
      start_autoconnecting_to_server();
    }
    if (irrg_menu_take_observer()) {
      connect_initiated = true;
      connect_observer = true;
      /* The FreeCiv protocol has NO client-side "become observer" request:
       * the SERVER grants it (console: `observe <name>`), at which point it
       * sends the full map and client_is_global_observer() flips TRUE. The
       * client auto-detects that (step 0d2) and re-centers. For a local test
       * server, run run-observer.sh so the client's connection is observed. */
      {
        std::string om = std::string("connecting to ") + server_host + ":" +
                         std::to_string(server_port) +
                         " as OBSERVER (server must grant: observe " +
                         std::string(user_name) + ")";
        irrg_log(om.c_str());
      }
      /* On-screen hint (shown in the always-visible message window): the
       * client alone cannot grant observer status, so tell the user how. */
      irrg_message_append(
          "Observer: the server must grant this (the client can't ask). "
          "Local game: run run-observer.sh instead of starting the server+"
          "client separately. Remote: ask the admin to run 'observe "
          "<yourname>'. The full map appears a moment after it's granted.");
      start_autoconnecting_to_server();
    }
    if (irrg_menu_take_quit()) {
      irrg_log("quit requested from main menu.");
      if (g_device) g_device->closeDevice();
    }
    if (connect_initiated && client_state() == C_S_DISCONNECTED)
      connect_initiated = false;   /* connect failed/aborted -> offer the menu again */
    const bool show_menu = !want_autoc
                           && client_state() <= C_S_DISCONNECTED
                           && !connect_initiated;
    {
      core::dimension2d<u32> msz = g_vdriver ? g_vdriver->getScreenSize()
                                             : core::dimension2d<u32>(900, 600);
      irrg_menu_update(show_menu, (int)msz.Width, (int)msz.Height);
    }

    /* 2d. FLIPTEST: draw a vertical gradient (row0=BLUE at the canvas TOP,
     *     lastrow=RED at the BOTTOM) and present it. A CORRECT present shows
     *     blue at the screen top / red at the bottom; a FLIPPED present shows
     *     the opposite. Used to detect a vertically-upside-down canvas on each
     *     driver. FC_IRR_FLIPTEST=1. */
    if (std::getenv("FC_IRR_FLIPTEST") && g_vdriver) {
      core::dimension2d<u32> fsz = g_vdriver->getScreenSize();
      static struct canvas *fcv = nullptr;
      if (!fcv || fcv->width != fsz.Width || fcv->height != fsz.Height) {
        if (fcv) irrg_canvas_free(fcv);
        fcv = irrg_canvas_create((int)fsz.Width, (int)fsz.Height);
      }
      if (fcv) {
        video::SColor *pp = (video::SColor *)fcv->pixels;
        for (int y = 0; y < fcv->height; ++y) {
          int t = (int)((float)y / (float)fcv->height * 255.0f); /* 0..255 */
          for (int x = 0; x < fcv->width; ++x)
            pp[(size_t)y * fcv->width + x] =
                video::SColor(255, t, 0, 255 - t);  /* row0: blue, bottom: red */
        }
        g_vdriver->beginScene(true, true, video::SColor(255, 0, 0, 0));
        irrg_canvas_present(fcv);
        g_vdriver->endScene();
      }
      continue;
    }

    /* 3. Render.
     * Priority: new-game options > settings menu > PREPARING status screen >
     * main menu > 3D map > 2D map canvas. The two modals (new-game, settings)
     * are full-window screens that own the frame while open. */

    /* 3a0. Modal screens: poll close/cancel/start requests + sync active state
     *      each frame. "Start Game" sends the configured server options and /start. */
    if (irrg_settings_take_close()) g_settings_open = false;
    if (irrg_gamemenu_take_close()) g_gamemenu_open = false;
    if (irrg_newgame_take_cancel()) g_newgame_open  = false;
    if (irrg_newgame_take_start()) {
      g_newgame_open = false;
      irrg_log("new game: sending options + /start");
      irrg_newgame_send_options();
    }
    {
      core::dimension2d<u32> msz3 = g_vdriver ? g_vdriver->getScreenSize()
                                              : core::dimension2d<u32>(900, 600);
      irrg_settings_update(g_settings_open, (int)msz3.Width, (int)msz3.Height);
      irrg_gamemenu_update(g_gamemenu_open, (int)msz3.Width, (int)msz3.Height);
      irrg_newgame_update(g_newgame_open,  (int)msz3.Width, (int)msz3.Height);
    }
    /* Headless test hooks (off by default): FC_IRR_TESTNEWGAME=1 auto-configures
     * and starts a game (Medium/Horizontal/4AI/Hard); FC_IRR_TESTNEWGAME_UI=1
     * opens the new-game screen and holds it for a screenshot. */
    if (client_state() == C_S_PREPARING) {
      static bool ng_test_done = false;
      if (std::getenv("FC_IRR_TESTNEWGAME") && !ng_test_done) {
        ng_test_done = true;
        irrg_log("TESTNEWGAME: sending Large/Horizontal/4AI/Hard + /start");
        irrg_newgame_test_send(2, 1, 4, 2);
      }
      if (std::getenv("FC_IRR_TESTNEWGAME_UI"))
        irrg_open_newgame();
    }
    /* FC_IRR_TESTSETTINGS=1: hold the graphics settings menu open (C_S_RUNNING)
     * for a screenshot. */
    if (client_state() == C_S_RUNNING && std::getenv("FC_IRR_TESTSETTINGS"))
      irrg_open_settings();
    /* FC_IRR_TESTMENU=1: hold the in-game menu open (C_S_RUNNING) for a
     * screenshot (the ESC-with-no-dialog entry point to every in-game action). */
    if (client_state() == C_S_RUNNING && std::getenv("FC_IRR_TESTMENU"))
      irrg_open_gamemenu();
    /* FC_IRR_TEST3D=1: lazily build + show the 3D map at runtime (tests the
     * Settings > 3D Terrain toggle path). */
    if (client_state() == C_S_RUNNING && std::getenv("FC_IRR_TEST3D")) {
      static bool did3d = false;
      if (!did3d) { did3d = true; irrg_set_use3d(true); }
    }

    /* 3a1. New-game options screen (modal, from the preparing screen). */
    if (g_newgame_open && g_vdriver) {
      core::dimension2d<u32> ng = g_vdriver->getScreenSize();
      static struct canvas *ngc = nullptr;
      if (!ngc || ngc->width != ng.Width || ngc->height != ng.Height) {
        if (ngc) irrg_canvas_free(ngc);
        ngc = irrg_canvas_create((int)ng.Width, (int)ng.Height);
      }
      g_vdriver->beginScene(true, true, video::SColor(255, 18, 20, 28));
      if (ngc) { irrg_newgame_draw(ngc, (int)ng.Width, (int)ng.Height); irrg_canvas_present(ngc); }
      g_vdriver->endScene();
    } else if (g_settings_open && g_vdriver) {
      /* 3a2. In-game graphics settings (modal). */
      core::dimension2d<u32> st = g_vdriver->getScreenSize();
      static struct canvas *stc = nullptr;
      if (!stc || stc->width != st.Width || stc->height != st.Height) {
        if (stc) irrg_canvas_free(stc);
        stc = irrg_canvas_create((int)st.Width, (int)st.Height);
      }
      g_vdriver->beginScene(true, true, video::SColor(255, 18, 20, 28));
      if (stc) { irrg_settings_draw(stc, (int)st.Width, (int)st.Height); irrg_canvas_present(stc); }
      g_vdriver->endScene();
    } else if (g_gamemenu_open && g_vdriver) {
      /* 3a2b. In-game menu (modal): the GUI path to every in-game action. */
      core::dimension2d<u32> gm = g_vdriver->getScreenSize();
      static struct canvas *gmc = nullptr;
      if (!gmc || gmc->width != gm.Width || gmc->height != gm.Height) {
        if (gmc) irrg_canvas_free(gmc);
        gmc = irrg_canvas_create((int)gm.Width, (int)gm.Height);
      }
      g_vdriver->beginScene(true, true, video::SColor(255, 18, 20, 28));
      if (gmc) { irrg_gamemenu_draw(gmc, (int)gm.Width, (int)gm.Height); irrg_canvas_present(gmc); }
      /* FC_IRR_DUMPWIN=/path.ppm: dump the menu's CPU canvas (createScreenShot
       * can't be used here -- the modal branch, not the 2D branch, renders).
       * The SW driver renders slowly, so dump ONCE after a few settled draws
       * rather than on a frames%N cadence. Off unless FC_IRR_DUMPWIN is set. */
      if (gmc && std::getenv("FC_IRR_DUMPWIN")) {
        static int gm_draws = 0;
        if (++gm_draws == 10) irrg_gamemenu_dump = true;  /* SW driver is slow */
      }
      if (gmc && irrg_gamemenu_dump) {
        irrg_gamemenu_dump = false;
        const char *dw = std::getenv("FC_IRR_DUMPWIN");
        if (dw) {
          std::FILE *f = std::fopen(dw, "wb");
          if (f) {
            std::fprintf(f, "P6\n%d %d\n255\n", gmc->width, gmc->height);
            const video::SColor *pp = (const video::SColor *)gmc->pixels;
            for (int i = 0; i < gmc->width * gmc->height; ++i) {
              unsigned char rgb[3] = { pp[i].getRed(), pp[i].getGreen(), pp[i].getBlue() };
              std::fwrite(rgb, 1, 3, f);
            }
            std::fclose(f);
            irrg_log((std::string("dumped gamemenu ") + dw + " " +
                      std::to_string(gmc->width) + "x" + std::to_string(gmc->height)).c_str());
          }
        }
      }
      g_vdriver->endScene();
    } else if (map3d && wld.map.xsize > 0 && wld.map.ysize > 0
               && !irrg_map3d_is_built() && g_vdriver) {
      /* 3a3. The 3D terrain isn't built yet (the map is still arriving from the
       * server and/or the build is pending). Show the LIVE 2D map (always
       * available from mapview.store) with a small "Building 3D terrain" status
       * line -- the previous working mechanism. There is deliberately NO
       * progress bar: a bar sits at a fixed % whenever the build is slow or a
       * terrain mesh can't be produced (slow download, missing art), which
       * looks exactly like a hang. The per-frame build retry (step 0d2) flips
       * us to the 3D view the moment the terrain is present and the mesh
       * builds; if it can't, the player still has a usable 2D map. */
      core::dimension2d<u32> ssz = g_vdriver->getScreenSize();
      int W = (int)ssz.Width, H = (int)ssz.Height;
      g_vdriver->beginScene(true, true, video::SColor(255, 30, 30, 30));
      struct canvas *mc = mapview.store ? mapview.store : irrg_get_map_canvas();
      if (mc && g_map_zoom_present > 1.0001f)
        irrg_canvas_present_zoomed(mc, g_map_zoom_present, W, H);
      else if (mc)
        irrg_canvas_present(mc);
      /* Small top banner: a plain status line, not a progress indicator. */
      {
        static struct canvas *bcv = nullptr;
        if (!bcv || bcv->width != W || bcv->height != H) {
          if (bcv) irrg_canvas_free(bcv);
          bcv = irrg_canvas_create(W, H);
        }
        if (bcv) {
          std::memset(bcv->pixels, 0, (size_t)W * H * 4);   /* transparent */
          static struct color bb_bg = { 24, 26, 34 };
          static struct color bb_tx = { 210, 224, 244 };
          irrg_canvas_put_rectangle(bcv, &bb_bg, 0, 0, W, 34);
          const char *bt = "Building 3D terrain ... (2D map shown until ready)";
          int tw = 0, th = 0;
          irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT, bt);
          irrg_canvas_put_text(bcv, W / 2 - tw / 2, (34 - th) / 2,
                               FONT_REQTREE_TEXT, &bb_tx, bt);
          irrg_canvas_present(bcv);
        }
      }
      g_vdriver->endScene();
    } else if (client_state() == C_S_PREPARING && g_vdriver) {
      /* 3a. PREPARING: connected but NO GAME running -> no map (mapview.store
       * null). Show a status screen; ENTER opens the new-game options screen,
       * ESC disconnects (back to the menu). Key handling in irrg_interact.cpp. */
      core::dimension2d<u32> psz = g_vdriver->getScreenSize();
      static struct canvas *pcv = nullptr;
      if (!pcv || pcv->width != psz.Width || pcv->height != psz.Height) {
        if (pcv) irrg_canvas_free(pcv);
        pcv = irrg_canvas_create((int)psz.Width, (int)psz.Height);
      }
      if (pcv) {
        static struct color pc_bg    = { 16, 16, 20 };
        static struct color pc_title = { 245, 245, 245 };
        static struct color pc_body  = { 205, 205, 210 };
        static struct color pc_dim   = { 150, 150, 162 };
        static struct color pc_hl    = { 110, 205, 255 };
        irrg_canvas_put_rectangle(pcv, &pc_bg, 0, 0, pcv->width, pcv->height);
        const char *host = server_host[0] ? server_host : "localhost";
        int port = (server_port > 0) ? server_port : 3000;
        const char *who = user_name[0] ? user_name : "(default)";
        char l_conn[200];
        std::snprintf(l_conn, sizeof l_conn, "Connected to %s:%d as %s",
                      host, port, who);
        int W = pcv->width, H = pcv->height, y = H / 2 - 140;
        auto putline = [&] (const char *s, enum client_font f, struct color *c) {
          int tw = 0, th = 0;
          irrg_get_text_size(&tw, &th, f, s);
          irrg_canvas_put_text(pcv, W / 2 - tw / 2, y, f, c, s);
          y += th + 12;
        };
        putline("FreeCiv 3D", FONT_CITY_NAME, &pc_title); y += 16;
        putline(l_conn, FONT_REQTREE_TEXT, &pc_body);
        putline("Status: connected, but no game is running yet (no map to show).",
                FONT_REQTREE_TEXT, &pc_dim);
        /* Two clickable buttons (mouse-friendly equivalents of ENTER/ESC) so a
         * mouse-only user can start a game or disconnect. Their rects are stored
         * in g_prep_* for irrg_prep_button_hit (called from irrg_interact.cpp). */
        {
          static struct color b1_bg  = { 44, 84, 128 };
          static struct color b1_bdr = { 120, 205, 255 };
          static struct color b1_tx  = { 238, 246, 255 };
          static struct color b2_bg  = { 58, 44, 48 };
          static struct color b2_bdr = { 178, 118, 128 };
          const int bw2 = 480, bh1 = 54, bh2 = 46;
          const char *s1 = "Start a New Game   (choose map options)";
          const char *s2 = "Disconnect and return to the menu";
          /* Start a New Game */
          g_prep_start_w = bw2; g_prep_start_h = bh1;
          g_prep_start_x = W / 2 - bw2 / 2; g_prep_start_y = H / 2 + 24;
          irrg_canvas_put_rectangle(pcv, &b1_bg,  g_prep_start_x, g_prep_start_y, bw2, bh1);
          irrg_canvas_put_rectangle(pcv, &b1_bdr, g_prep_start_x, g_prep_start_y, bw2, 2);
          irrg_canvas_put_rectangle(pcv, &b1_bdr, g_prep_start_x, g_prep_start_y + bh1 - 2, bw2, 2);
          irrg_canvas_put_rectangle(pcv, &b1_bdr, g_prep_start_x, g_prep_start_y, 2, bh1);
          irrg_canvas_put_rectangle(pcv, &b1_bdr, g_prep_start_x + bw2 - 2, g_prep_start_y, 2, bh1);
          { int tw=0, th=0; irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT, s1);
            irrg_canvas_put_text(pcv, g_prep_start_x + (bw2 - tw)/2,
                                 g_prep_start_y + (bh1 - th)/2, FONT_REQTREE_TEXT, &b1_tx, s1); }
          /* Disconnect */
          g_prep_disc_w = bw2; g_prep_disc_h = bh2;
          g_prep_disc_x = W / 2 - bw2 / 2; g_prep_disc_y = g_prep_start_y + bh1 + 12;
          irrg_canvas_put_rectangle(pcv, &b2_bg,  g_prep_disc_x, g_prep_disc_y, bw2, bh2);
          irrg_canvas_put_rectangle(pcv, &b2_bdr, g_prep_disc_x, g_prep_disc_y, bw2, 2);
          irrg_canvas_put_rectangle(pcv, &b2_bdr, g_prep_disc_x, g_prep_disc_y + bh2 - 2, bw2, 2);
          irrg_canvas_put_rectangle(pcv, &b2_bdr, g_prep_disc_x, g_prep_disc_y, 2, bh2);
          irrg_canvas_put_rectangle(pcv, &b2_bdr, g_prep_disc_x + bw2 - 2, g_prep_disc_y, 2, bh2);
          { int tw=0, th=0; irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT, s2);
            irrg_canvas_put_text(pcv, g_prep_disc_x + (bw2 - tw)/2,
                                 g_prep_disc_y + (bh2 - th)/2, FONT_REQTREE_TEXT, &b1_tx, s2); }
        }
        if (connect_observer) {
          int oy = g_prep_disc_y + g_prep_disc_h + 22;
          auto putobs = [&] (const char *s) {
            int tw = 0, th = 0;
            irrg_get_text_size(&tw, &th, FONT_REQTREE_TEXT, s);
            irrg_canvas_put_text(pcv, W / 2 - tw / 2, oy, FONT_REQTREE_TEXT, &pc_dim, s);
            oy += th + 6;
          };
          putobs("You chose OBSERVER: the server must grant that (the client");
          putobs("cannot ask for it). Local: run-observer.sh. Remote: ask the");
          putobs("server admin to run:  observe <yourname>");
        }
        g_vdriver->beginScene(true, true, video::SColor(255, 16, 16, 20));
        irrg_canvas_present(pcv);
        g_vdriver->endScene();
      }
    } else if (show_menu && g_vdriver) {
      core::dimension2d<u32> msz2 = g_vdriver->getScreenSize();
      static struct canvas *mnc = nullptr;
      if (!mnc || mnc->width != msz2.Width || mnc->height != msz2.Height) {
        if (mnc) irrg_canvas_free(mnc);
        mnc = irrg_canvas_create((int)msz2.Width, (int)msz2.Height);
      }
      g_vdriver->beginScene(true, true, video::SColor(255, 20, 22, 30));
      if (mnc) {
        irrg_menu_draw(mnc, (int)msz2.Width, (int)msz2.Height);
        irrg_canvas_present(mnc);
      }
      g_vdriver->endScene();
    } else if (irrg_map3d_is_built() && g_use3d) {
      /* Render 3D + the 2D dialog overlay inside ONE beginScene/endScene so the
       * window is presented exactly once per frame. Rendering 3D to its own scene
       * and then presenting the overlay in a separate scene flickered: the overlay
       * only reached the window on ~half the frames under the software driver.
       * g_use3d (Settings > 3D Terrain) can turn this off in favour of the 2D view. */
      if (g_vdriver) {
        irrg_map3d_refresh_tiles(); /* add newly-revealed terrain immediately */
        core::dimension2d<u32> ssz = g_vdriver->getScreenSize();
        /* Civ4-style edge-hover pan: hover a window edge to slide the map that
         * way (seamless across the map edges when it wraps). */
        {
          int emx = -1, emy = -1;
          irrg_mouse_get(&emx, &emy);
          if (emx >= 0)
            irrg_map3d_edge_pan((int)ssz.Width, (int)ssz.Height, emx, emy);
        }
        g_vdriver->beginScene(true, true, video::SColor(255, 70, 130, 180));
        irrg_map3d_draw_units();   /* sync unit billboards + selection ring */
        irrg_map3d_draw_cities_and_resources(); /* city + resource billboards */
        irrg_map3d_draw3d();       /* drawAll() renders terrain + billboards */
        irrg_present_dialog_overlay((int)ssz.Width, (int)ssz.Height);
        g_vdriver->endScene();
      }
    } else if (g_vdriver) {
      /* 2D: present the map (zoomed if FC_IRR_ZOOM>1), then the dialogs as a
       * transparent overlay on top -- the same map-then-overlay pattern the 3D
       * path uses, so the dialogs stay at fixed window coordinates (not zoomed
       * with the map). */
      core::dimension2d<u32> ssz2 = g_vdriver->getScreenSize();
      g_vdriver->beginScene(true, true, video::SColor(255, 30, 30, 30));
      /* Present mapview.store (the CURRENT backing store), not the cached
       * irrg_get_map_canvas(): update_map_canvas swaps store<->tmp_store on every
       * partial redraw, so the cached pointer goes stale and we'd present the
       * old canvas (gui-sdl2/gtk present mapview.store for exactly this reason). */
      struct canvas *mc = mapview.store ? mapview.store : irrg_get_map_canvas();
      if (mc && std::getenv("FC_IRR_MARKUNITS"))
        irrg_mark_units(mc);
      if (mc && g_map_zoom_present > 1.0001f)
        irrg_canvas_present_zoomed(mc, g_map_zoom_present,
                                   (int)ssz2.Width, (int)ssz2.Height);
      else
        irrg_canvas_present(mc);
      irrg_present_dialog_overlay((int)ssz2.Width, (int)ssz2.Height);
      /* FC_IRR_DUMPWIN=/path.ppm: capture the full window (map + dialogs/menu)
       * to verify the overlay UI headlessly. createScreenShot reads the back
       * buffer (a different path from RT lock(), which is garbage on the SW
       * driver) and works here. */
      if ((frames % 300) == 0 && frames > 0) {
        const char *dw = std::getenv("FC_IRR_DUMPWIN");
        if (dw) {
          video::IImage *img = g_vdriver->createScreenShot();
          if (img) {
            u32 WW = img->getDimension().Width, HH = img->getDimension().Height;
            std::FILE *f = std::fopen(dw, "wb");
            if (f) {
              std::fprintf(f, "P6\n%d %d\n255\n", WW, HH);
              for (u32 y = 0; y < HH; ++y)
                for (u32 x = 0; x < WW; ++x) {
                  video::SColor c = img->getPixel(x, y);
                  unsigned char rgb[3] = { c.getRed(), c.getGreen(), c.getBlue() };
                  std::fwrite(rgb, 1, 3, f);
                }
              std::fclose(f);
              irrg_log((std::string("dumped window ") + dw + " " +
                        std::to_string(WW) + "x" + std::to_string(HH)).c_str());
            }
            img->drop();
          }
        }
      }
      g_vdriver->endScene();
    }

    /* 3a. Phase 5 diagnostics: FC_IRR_DUMP3D=/path.ppm dumps a 3D screenshot
     * (once the map is built) and FC_IRR_PICK=1 logs the tile under the screen
     * center, so 3D rendering + picking can be verified headlessly. */
    if (irrg_map3d_is_built() && (frames % 300) == 0 && frames > 0) {
      const char *dump3d = std::getenv("FC_IRR_DUMP3D");
      if (dump3d && g_vdriver) {
        video::IImage *img = g_vdriver->createScreenShot();
        if (img) {
          u32 W = img->getDimension().Width, H = img->getDimension().Height;
          std::FILE *f = std::fopen(dump3d, "wb");
          if (f) {
            std::fprintf(f, "P6\n%d %d\n255\n", W, H);
            for (u32 y = 0; y < H; ++y)
              for (u32 x = 0; x < W; ++x) {
                video::SColor c = img->getPixel(x, y);
                unsigned char rgb[3] = { c.getRed(), c.getGreen(), c.getBlue() };
                std::fwrite(rgb, 1, 3, f);
              }
            std::fclose(f);
            irrg_log((std::string("dumped 3D map ") + dump3d +
                      " (" + std::to_string(W) + "x" + std::to_string(H) + ")").c_str());
          }
          img->drop();
        }
      }
      const char *pick = std::getenv("FC_IRR_PICK");
      if (pick) {
        int px = 0, py = 0;
        core::dimension2d<u32> sz = g_vdriver->getScreenSize();
        if (irrg_map3d_pick((int)(sz.Width / 2), (int)(sz.Height / 2), &px, &py))
          irrg_log((std::string("pick@center -> map tile (") +
                    std::to_string(px) + "," + std::to_string(py) + ")").c_str());
        else
          irrg_log("pick@center -> no hit");
      }
    }

    /* 3b. Diagnostic: FC_IRR_SELFTEST=/path.ppm runs a one-shot 2D self-test
     * (background + rectangle + text + a loaded flag sprite), presents it, and
     * dumps the canvas buffer to a PPM so the 2D primitives can be verified. */
    if (frames == 100) {
      const char *st = std::getenv("FC_IRR_SELFTEST");
      if (st) {
        struct canvas *tc = irrg_canvas_create(400, 300);
        struct color *bg = irrg_color_alloc(40, 60, 90);
        irrg_canvas_put_rectangle(tc, bg, 0, 0, 400, 300);
        irrg_color_free(bg);
        struct color *red = irrg_color_alloc(220, 40, 40);
        irrg_canvas_put_rectangle(tc, red, 20, 20, 120, 80);
        irrg_color_free(red);
        struct color *white = irrg_color_alloc(255, 255, 255);
        irrg_canvas_put_text(tc, 20, 120, FONT_CITY_NAME, white,
                             "FreeCiv Irrlicht Phase 4 2D self-test");
        irrg_color_free(white);
        struct sprite *flag =
          irrg_load_gfxfile("data/flags/aborigines-large.png", false);
        if (flag) {
          irrg_canvas_put_sprite_full(tc, 20, 160, flag);
          irrg_free_sprite(flag);
        }
        irrg_canvas_present(tc);
        std::FILE *f = std::fopen(st, "wb");
        if (f) {
          std::fprintf(f, "P6\n%d %d\n255\n", tc->width, tc->height);
          const video::SColor *px = (const video::SColor *)tc->pixels;
          for (int i = 0; i < tc->width * tc->height; ++i) {
            unsigned char rgb[3] = { px[i].getRed(), px[i].getGreen(), px[i].getBlue() };
            std::fwrite(rgb, 1, 3, f);
          }
          std::fclose(f);
          irrg_log((std::string("self-test canvas dumped to ") + st).c_str());
        }
        irrg_canvas_free(tc);
      }
    }

    /* 3c. Diagnostic: FC_IRR_DUMP=/path.ppm dumps the real map canvas
     * periodically (once it exists in C_S_RUNNING) to verify map rendering.
     * Interval is 120 frames (fast enough to fire under slow software render). */
    if ((frames % 120) == 0 && frames > 0) {
      const char *dump = std::getenv("FC_IRR_DUMP");
      if (dump) {
        /* Dump the SAME canvas the present path shows (mapview.store), not the
         * cached irrg_get_map_canvas() pointer, which update_map_canvas swaps
         * out on partial redraws and which then reads as all-black. */
        struct canvas *mc = mapview.store ? mapview.store : irrg_get_map_canvas();
        if (mc && mc->pixels) {
          std::FILE *f = std::fopen(dump, "wb");
          if (f) {
            std::fprintf(f, "P6\n%d %d\n255\n", mc->width, mc->height);
            const video::SColor *px = (const video::SColor *)mc->pixels;
            for (int i = 0; i < mc->width * mc->height; ++i) {
              unsigned char rgb[3] = { px[i].getRed(), px[i].getGreen(), px[i].getBlue() };
              std::fwrite(rgb, 1, 3, f);
            }
            std::fclose(f);
            std::string msg = std::string("dumped map canvas ") + dump +
              " (" + std::to_string(mc->width) + "x" +
              std::to_string(mc->height) + ") state=" +
              std::to_string((int)client_state());
            irrg_log(msg.c_str());
          }
        }
      }
    }

    if ((++frames % 300) == 0) {
      const int st = (int)client_state();
      std::string hb = std::string("event loop alive: frames=") + std::to_string(frames)
        + " netsock=" + std::to_string(net_socket)
        + " state=" + std::to_string(st)
        + " map=" + std::to_string(wld.map.xsize) + "x" + std::to_string(wld.map.ysize)
        + " map_init=" + (map_initialized ? "1" : "0")
        + " 3d=" + (map3d ? "1" : "0")
        + " built=" + (irrg_map3d_is_built() ? "1" : "0");
      /* Self-diagnosis: if 3D is wanted but not up, say WHY (so a stuck
       * client is explainable from a single log line). */
      if (map3d && !irrg_map3d_is_built()) {
        if (st < 3)
          hb += "   <- 3D idle: not in a running game yet (state<3) -- no map to build";
        else if (wld.map.xsize <= 0 || wld.map.ysize <= 0)
          hb += "   <- 3D idle: map size not received yet";
        else
          hb += "   <- 3D idle: terrain not built (look for the 'no tiles rendered ... art=' line)";
      }
      irrg_log(hb.c_str());
    }
  }
  irrg_log("ui_main: event loop exited.");
  return 0;
}

void irrg_ui_exit(void)
{
  irrg_log("ui_exit: closing device.");
  if (g_device) { g_device->closeDevice(); g_device->drop(); g_device = 0; }
  g_scmgr = 0; g_vdriver = 0; g_guienv = 0;
}

/* ---- Identity / info ---- */
enum gui_type irrg_get_gui_type(void) { return GUI_IRRLICHT; }

void irrg_insert_client_build_info(char *outbuf, size_t outlen)
{
  if (outbuf && outlen) std::snprintf(outbuf, outlen, "irrlicht");
}

void irrg_version_message(const char *vertext)
{
  if (vertext) std::fprintf(stderr, "%s\n", vertext);
}

void irrg_real_output_window_append(const char *astring,
                                    const struct text_tag_list *tags, int conn_id)
{
  (void)tags; (void)conn_id;
  if (astring) {
    std::fprintf(stderr, "%s", astring);
    irrg_message_append(astring);   /* Phase 6: show in the message window */
  }
}

/* ---- Event-loop hooks (functional in Phase 3) ---- */
void irrg_add_net_input(int sock)
{
  net_socket = sock;
  irrg_log("add_net_input(sock).");
}
void irrg_remove_net_input(void)
{
  net_socket = -1;
  irrg_log("remove_net_input.");
}
void irrg_add_idle_callback(void (callback)(void *), void *data)
{
  struct irrg_idle *c = (struct irrg_idle *)std::malloc(sizeof(*c));
  c->cb = callback;
  c->data = data;
  c->next = idle_head;
  idle_head = c;
}

/* ---- main ---- */
/* Defined in client/client_main.c (char forced_tileset_name[512]); the
 * --tiles option sets it. We pre-set it to the project's SQUARE tileset so
 * new (square-topology) maps render with square tiles. A user --tiles X
 * override still wins, because client_main() re-sets it when that option is
 * present. */
extern char forced_tileset_name[];

/* Whether to force square tiles (the client's default presentation). Off with
 * FC_IRR_SQRT=0 so a square-tileset client can be pointed at an iso/hex server
 * for comparison. (The map-topology forcing itself lives in packhand.c, gated
 * by the same flag.) */
static bool irrg_force_square_tiles(void)
{
  static int cache = -1;
  if (cache < 0) {
    const char *e = std::getenv("FC_IRR_SQRT");
    cache = (e && e[0] == '0') ? 0 : 1;
  }
  return cache == 1;
}

int main(int argc, char **argv)
{
  setup_gui_funcs();
  /* Default to a SQUARE tileset (this client renders a square x/y grid). "3d"
   * (our demo tileset) is incomplete and fails tilespec_try_read, so use the
   * stock, complete square "Trident" tileset instead. Overridable with
   * FC_IRR_TILESET=<name>; the --tiles option (parsed in client_main, after
   * this) wins over both. FC_IRR_SQRT=0 disables the forcing entirely. */
  if (forced_tileset_name[0] == '\0' && irrg_force_square_tiles()) {
    const char *ts = std::getenv("FC_IRR_TILESET");
    const char *want = (ts && ts[0] != '\0') ? ts : "trident";
    std::snprintf(forced_tileset_name, 512, "%s", want);
  }
  /* postpone_tileset=FALSE: let client_main() run default_tileset_select()
   * (loads a tileset via tilespec_try_read, no dialog) so the global `tileset`
   * is non-NULL before the server's ruleset/map packets arrive. */
  return client_main(argc, argv, /* postpone_tileset= */ false);
}
