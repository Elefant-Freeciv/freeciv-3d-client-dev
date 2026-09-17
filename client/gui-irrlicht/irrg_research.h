#ifndef IRRG_RESEARCH_H
#define IRRG_RESEARCH_H

/*
 * gui-irrlicht: tech research selection menu (a Civ4-style overlay panel).
 *
 * Opened from the top-bar "Research" button (or the in-game menu). It lists the
 * techs the player can research next (all prerequisites already known), shows
 * the current research project + tech goal, and lets the player pick a new
 * project -- which sends PACKET_PLAYER_RESEARCH (dsend_packet_player_research)
 * to change it. Works with mouse (click a row) and keyboard (up/down + Enter).
 *
 * The panel is drawn as an overlay (irrg_research_draw, called from
 * irrg_draw_dialogs) and routed in irrg_interact.cpp; it is self-contained --
 * sending the packet happens inside this module, so gui_main needs no changes.
 */

struct canvas;

void irrg_research_open(void);
void irrg_research_close(void);
bool irrg_research_is_open(void);

/* Draw the panel (no-op unless open). */
void irrg_research_draw(struct canvas *cv);

/* Return true if the click was consumed (over the panel / a row / close). */
bool irrg_research_handle_click(int mx, int my);

/* Keyboard nav (up/down/enter/esc). `key` is the Irrlicht EKEY code. Returns
 * true if the key was consumed (any key while the panel is open). */
bool irrg_research_handle_key(int key, bool press);

#endif /* IRRG_RESEARCH_H */
