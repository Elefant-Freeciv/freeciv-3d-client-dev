#ifndef IRRG_MENU_H
#define IRRG_MENU_H

/* Main-menu (pre-game) screen shown before the client is connected.
 *
 * A normal FreeCiv client shows its connect/main-menu here; the irrg client
 * used to draw nothing (a blank grey window) until a connection was made. This
 * module provides a simple keyboard + mouse menu: "Connect to Server" (uses the
 * --server/--port/--name defaults) and "Quit".
 *
 * The main loop calls irrg_menu_update() each frame with the window size and
 * whether the menu should be shown; the event receiver calls irrg_menu_on_key /
 * irrg_menu_on_click; the main loop polls irrg_menu_take_connect / _quit for
 * pending actions and renders with irrg_menu_draw().
 */

struct canvas;

/* Set whether the menu is active and the current window size (for layout and
 * mouse hit-testing). Called once per frame by the main loop. */
void irrg_menu_update(bool active, int win_w, int win_h);

/* True while the menu is active (the event receiver routes input to it). */
bool irrg_menu_is_active(void);

/* Input hooks (called by the Irrlicht event receiver). */
void irrg_menu_on_key(int ekey);
void irrg_menu_on_click(int x, int y);
void irrg_menu_on_mouse_move(int x, int y);   /* hover highlight */
void irrg_menu_on_mouse_down(int x, int y);   /* pressed (sunken) look */
void irrg_menu_on_mouse_up(int x, int y);     /* release */

/* Poll-and-clear a pending action. take_connect = join as a player;
 * take_observer = join as an observer (full map; the server must grant it,
 * see AGENTS.md "Connect as observer"). */
bool irrg_menu_take_connect(void);
bool irrg_menu_take_observer(void);
bool irrg_menu_take_quit(void);

/* Draw the menu onto a full-window canvas (win_w x win_h). */
void irrg_menu_draw(struct canvas *cv, int win_w, int win_h);

#endif /* IRRG_MENU_H */
