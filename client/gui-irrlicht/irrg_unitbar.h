/* irrg_unitbar.h -- unit action buttons (Phase 5b)
 *
 * A bottom-centre bar of buttons for the currently focused (selected) unit.
 * Each button sends a gameplay action via FreeCiv's high-level request_* API.
 * The bar is drawn as part of the 2D overlay (so it works in both the 3D and
 * 2D map views) and receives clicks through irrg_unitbar_handle_click().
 */
#ifndef IRRG_UNITBAR_H
#define IRRG_UNITBAR_H

#include "graphics.h"   /* struct canvas */

/* Draw the bar (no-op when it is not visible). */
void irrg_unitbar_draw(struct canvas *cv);
/* Returns true if (mx,my) hit a button (the matching action is sent). Also
 * clears any in-progress button press (it is called on left-mouse release). */
bool irrg_unitbar_handle_click(int mx, int my);
/* Feed the current cursor position (called on mouse-move) so the bar can
 * highlight the button under it. */
void irrg_unitbar_mouse_move(int mx, int my);
/* Called on left-mouse press: marks the button under the cursor as pressed
 * (drawn "down") until it is released. */
void irrg_unitbar_mouse_down(int mx, int my);
/* Clear any in-progress "pressed" look without dispatching (used when a left
 * press turns into a map pan instead of a button click). */
void irrg_unitbar_release(void);
/* In-game + a unit is focused + no modal dialog is up. */
bool irrg_unitbar_visible(void);

/* --- Auto Explore / Auto Worker (FreeCiv server-side agents) ---------------
 * Global toggles. When on, irrg_auto_tick() sets each eligible unit's SSA:
 * worker units -> SSA_AUTOWORKER (the server makes them improve tiles with
 * their moves), other units -> SSA_AUTOEXPLORE (the server makes them explore
 * new territory). The server performs the actual work; we only set the mode.
 * The tick is called each frame (throttled in gui_main) so newly-built units
 * pick up the current mode; it only sends a packet when a unit's SSA changes.
 * -------------------------------------------------------------------------- */
void irrg_auto_set_explore(bool on);
void irrg_auto_set_worker(bool on);
bool irrg_auto_explore(void);
bool irrg_auto_worker(void);
void irrg_auto_tick(void);

/* Test helper (FC_IRR_TESTIMPROVE): have the first unit that can improve its
 * current tile do so (mine/irrigate/clear/road) to verify 3D improvement
 * rendering. Returns true if an action was sent. */
bool irrg_test_improve(void);

#endif /* IRRG_UNITBAR_H */
