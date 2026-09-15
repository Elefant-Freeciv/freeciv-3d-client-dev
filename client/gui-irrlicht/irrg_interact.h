/***********************************************************************
 Freeciv - gui-irrlicht Phase 6: interactive map controls.
   Drains Irrlicht input events and maps them onto FreeCiv's shared mapctrl
   core (do_map_click / request_unit_goto / do_unit_goto). Also provides a
   headless unit-move verification (FC_IRR_SIMMOVE) so movement can be tested
   without a real mouse.
***********************************************************************/
#ifndef IRG_INTERACT_H
#define IRG_INTERACT_H

/* Register the mouse event receiver with the device (call once after the
 * device is created, before the event loop). Irrlicht 1.8.5 delivers input via
 * an IEventReceiver, whose OnEvent() is invoked by device->run() for each OS
 * event. Handles:
 *   left-click  -> do_map_click (select a tile/unit/city)
 *   right-click -> if a unit is in focus, goto/move it to the tile;
 *                  otherwise recenter the map on the tile
 *   mouse wheel -> zoom the 3D view */
void irrg_setup_input(void);

/* Headless unit-move verification (FC_IRR_SIMMOVE=1): find one of our movable
 * units, select it, and issue a goto to an adjacent walkable tile. Fills the
 * out params with the unit id and the from/to map coordinates. Returns TRUE if
 * a move order was actually sent. */
bool irrg_simmove(int *out_unit_id, int *out_from_x, int *out_from_y,
                  int *out_to_x, int *out_to_y);

/* Current map position of a unit (by id). Returns TRUE if the unit exists. */
bool irrg_unit_pos(int unit_id, int *out_x, int *out_y);

/* FC_IRR_DIAGUNIT: one-shot log of our cities/units' map + canvas positions. */
void irrg_diag_units_cities(void);

/* FC_IRR_SPAWNCITY: create a test city on our first unit tile (map editor). */
bool irrg_spawn_test_city(void);

/* FC_IRR_MARKUNITS: draw a bright box on the map store at each of our unit
 * tiles (red) / city tiles (cyan) so a dump shows where they land. */
struct canvas;
void irrg_mark_units(struct canvas *cv);

#endif /* IRG_INTERACT_H */
