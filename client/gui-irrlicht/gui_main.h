#ifndef GUI_IRRLICHT_GUI_MAIN_H
#define GUI_IRRLICHT_GUI_MAIN_H

/* gui_main.h - shared state + entry points for the irrg client.
 *
 * The main loop (gui_main.cpp) owns the runtime graphics state. The
 * in-game Settings menu (irrg_settings.cpp) reads/writes it through the
 * accessors below; the event receiver (irrg_interact.cpp) opens the modal
 * screens through irrg_open_settings / irrg_open_newgame.
 */

#include <stdbool.h>

/* --- Runtime graphics state (adjusted from the Settings menu) --- */

/* 2D map presentation zoom (1.0-4.0). Independent of FreeCiv's map_zoom. */
double irrg_get_map_zoom_present(void);
void   irrg_set_map_zoom_present(double zoom);

/* Fog of war (mirrors gui_options.draw_fog_of_war). */
bool   irrg_fog_enabled(void);
void   irrg_set_fog_enabled(bool on);

/* 3D terrain view toggle (only effective when the 3D map was built). */
bool   irrg_use3d(void);
void   irrg_set_use3d(bool on);

/* --- Modal screen openers (called by the event receiver) --- */

void irrg_open_settings(void);   /* in-game graphics settings ('G') */
void irrg_open_newgame(void);    /* new-game options (from the preparing screen) */
void irrg_open_gamemenu(void);   /* in-game menu ('ESC' with no dialog open) */

/* PREPARING screen (connected, no game yet): the two on-screen buttons let a
 * mouse-only user start a game / disconnect (the ENTER/ESC equivalents).
 * Returns 0 = "Start a New Game", 1 = "Disconnect", -1 = no button hit. */
int irrg_prep_button_hit(int x, int y);

#endif /* GUI_IRRLICHT_GUI_MAIN_H */
