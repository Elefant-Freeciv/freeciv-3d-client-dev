#ifndef IRRG_NEWGAME_H
#define IRRG_NEWGAME_H

/* "Start a new game" options screen.
 *
 * Shown in the C_S_PREPARING state when the user chooses to start a new game
 * (there is a connection but no running game yet). Instead of immediately
 * starting, it presents the configurable options and, on "Start Game", sends
 * the matching server-console commands over the (admin-level) chat line and
 * then "/start":
 *
 *   Map size      -> /set mapsize fullsize ; /set size <N>
 *   World wrap    -> /set wrap <wrapx|wrapy ...>
 *   AI opponents  -> /remove AI*<n> / /create AI*<n>  (classic aifill=6 base)
 *   Difficulty    -> /easy | /normal | /hard | /cheating
 *
 * The commands require the client to have server "hack"/ctrl access (the
 * normal local-server case). See AGENTS.md "Server console commands".
 */

struct canvas;

void irrg_newgame_update(bool active, int win_w, int win_h);
bool irrg_newgame_is_active(void);

void irrg_newgame_on_key(int ekey);
void irrg_newgame_on_click(int x, int y);
void irrg_newgame_on_mouse_move(int x, int y);   /* hover highlight */
void irrg_newgame_on_mouse_down(int x, int y);   /* pressed (sunken) look */
void irrg_newgame_on_mouse_up(int x, int y);     /* release */

bool irrg_newgame_take_start(void);
bool irrg_newgame_take_cancel(void);

/* Send the configured options as server commands, then /start. */
void irrg_newgame_send_options(void);

/* Headless test hook (FC_IRR_TESTNEWGAME): set specific options and send them.
 * Values use the same 0-based indices the UI uses. Off unless called. */
void irrg_newgame_test_send(int mapsize, int wrap, int ai, int diff);

void irrg_newgame_draw(struct canvas *cv, int win_w, int win_h);

#endif /* IRRG_NEWGAME_H */
