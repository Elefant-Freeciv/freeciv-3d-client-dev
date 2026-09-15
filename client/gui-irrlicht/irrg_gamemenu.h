#ifndef IRRG_GAMEMENU_H
#define IRRG_GAMEMENU_H

/* In-game Menu (opened with ESC while in the game and no dialog is showing).
 *
 * This is the single GUI entry point for every in-game action, so nothing is
 * reachable only by a hidden key. It lists:
 *   - Unit Reports   (R)
 *   - Economy Report (O)
 *   - Science Report (S)
 *   - Help           (H)
 *   - Graphics       (G)   -> opens the graphics settings menu
 *   - Fog of War     (F)   -> toggle on/off
 *   - End Turn               -> send the turn-done command (was previously
 *                               only possible headlessly -- no GUI path)
 *   - Quit to Main Menu (Q)  -> disconnect (back to the connect menu)
 *
 * Every item performs its action and closes the menu; ESC also closes it.
 * There is always a way out (Quit + Close), so no dead ends.
 *
 * The main loop calls irrg_gamemenu_update() each frame, renders with
 * irrg_gamemenu_draw(), and polls irrg_gamemenu_take_close(). The event
 * receiver routes input with irrg_gamemenu_on_key / _on_click while active.
 */

struct canvas;

void irrg_gamemenu_update(bool active, int win_w, int win_h);
bool irrg_gamemenu_is_active(void);

void irrg_gamemenu_on_key(int ekey);
void irrg_gamemenu_on_click(int x, int y);

/* Poll-and-clear the "close menu" request (ESC / after an action fires). */
bool irrg_gamemenu_take_close(void);

void irrg_gamemenu_draw(struct canvas *cv, int win_w, int win_h);

#endif /* IRRG_GAMEMENU_H */
