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

#endif /* IRRG_UNITBAR_H */
