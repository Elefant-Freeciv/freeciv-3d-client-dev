/*
 * gui-irrlicht: Phase 6 dialog surface.
 *
 * A minimal but real dialog layer on top of the 2D canvas:
 *   - a message window (recent game messages, bottom-left), and
 *   - a basic city dialog (name / population / size / current production).
 *
 * The FreeCiv dialog *logic* lives in the *_common.c files; the GUI side is
 * just: open (set state), draw (each frame), close (clear state). We hook the
 * open/close to the real gui funcs (real_city_dialog_popup / popdown_city_dialog)
 * and append messages from real_output_window_append.
 */
#ifndef IRRG_DIALOGS_H
#define IRRG_DIALOGS_H

#include "graphics.h"   /* struct canvas, struct color */

struct city;

/* Message window. */
void irrg_message_append(const char *text);
void irrg_draw_messages(struct canvas *cv);

/* Basic city dialog. */
void irrg_city_dialog_open(struct city *pcity);
void irrg_city_dialog_close(void);
bool   irrg_city_dialog_is_open(void);
bool   irrg_city_dialog_open_for(struct city *pcity);
void irrg_draw_city_dialog(struct canvas *cv);
/* City production-menu key input (Up/Down/PgUp/PgDn/Enter). Returns TRUE if
 * the key was consumed. `key` is an irrlicht E_KEY_CODE passed as int. */
bool irrg_city_dialog_handle_key(int key, bool press);
/* Headless test: select buildable #index and start production on it. */
bool irrg_city_dialog_test_build(int index);

/* Draw every open dialog (city dialog + message window) onto the canvas. */
void irrg_draw_dialogs(struct canvas *cv);
/* Present the dialogs as a transparent 2D overlay (used on the 3D path). */
void irrg_present_dialog_overlay(int w, int h);

/* Reports + Help (Phase 6). Opened via the report gui_funcs and keyboard
 * (H/R/O/S); ESC closes whichever is up. Kind: 0=units 1=science 2=economy. */
void irrg_reports_open(int kind);
void irrg_reports_close(void);
void irrg_help_open(void);
void irrg_help_close(void);
/* The "Messages" submenu (the game log as a panel, opened from the in-game
 * menu or the L key). Replaces the old always-on bottom-left message window. */
void irrg_log_open(void);
void irrg_log_close(void);
bool irrg_log_is_open(void);
bool irrg_unit_dialog_hit(int mx, int my);
/* End Turn button (top-right): draw it, hit-test it, and feed the cursor.
 * Clicking it ends the current player's turn (irrg_interact.cpp dispatches
 * user_ended_turn() when irrg_endturn_button_hit() is true on a left click). */
void irrg_draw_endturn_button(struct canvas *cv);
bool irrg_endturn_button_hit(int mx, int my);
void irrg_endturn_mouse_move(int mx, int my);
bool irrg_report_or_help_open(void);   /* true if a report/help/messages panel is up */
int  irrg_close_topmost_report_or_help(void);

/* In-game persistent "Menu" button (top-right, to the LEFT of End Turn): opens
 * the in-game menu -- the single GUI entry point for every action (reports,
 * help, settings, fog, end turn, quit) -- so the game is playable with a mouse
 * alone (no need to know the ESC shortcut). */
void irrg_draw_menu_button(struct canvas *cv);
bool irrg_menu_button_hit(int mx, int my);
void irrg_menu_button_mouse_move(int mx, int my);

/* City dialog mouse input: click a build-list row to start building it, or the
 * close button to dismiss. Returns TRUE if the click landed inside the dialog
 * (so the caller must not also hit-test the map behind it). */
bool irrg_city_dialog_click(int x, int y, bool left);

/* Report/Help/Log panels: a left click closes the topmost one (the mouse
 * equivalent of ESC). Returns TRUE if a panel was open and the click was
 * consumed (i.e. do not fall through to the map). */
bool irrg_panel_click(int x, int y);

/* Minimap (3D, bottom-right): a small colour map of the whole explored map +
 * a viewport rectangle for the 3D camera. Draw it, then hit-test a click
 * (on a hit *out_tx/*out_ty = the map tile to jump the camera to). */
void irrg_draw_minimap(struct canvas *cv);
int  irrg_minimap_hit(int x, int y, int *out_tx, int *out_ty);

#endif /* IRRG_DIALOGS_H */
