#ifndef IRRG_SETTINGS_H
#define IRRG_SETTINGS_H

/* In-game Graphics Settings menu.
 *
 * Opens over the running map (a modal full-window screen) and lets the user
 * adjust client-side graphics options, applied live:
 *   - 2D Map Zoom   (how much of the map fills the window; 1.0-4.0)
 *   - Fog of War    (on/off; see AGENTS.md "Fog of war" for the data note)
 *   - 3D Terrain    (on/off; only meaningful when the 3D map view is built)
 *
 * The main loop calls irrg_settings_update() each frame, renders with
 * irrg_settings_draw(), and polls irrg_settings_take_close(). The event
 * receiver routes input with irrg_settings_on_key / _on_click while active.
 */

struct canvas;

void irrg_settings_update(bool active, int win_w, int win_h);
bool irrg_settings_is_active(void);

void irrg_settings_on_key(int ekey);
void irrg_settings_on_click(int x, int y);

/* Poll-and-clear the "close menu" request (ESC / the Close row). */
bool irrg_settings_take_close(void);

void irrg_settings_draw(struct canvas *cv, int win_w, int win_h);

#endif /* IRRG_SETTINGS_H */
