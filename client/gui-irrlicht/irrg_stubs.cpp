/***********************************************************************
 Freeciv - gui-irrlicht: dialog / editor / report / theme / diplomacy
   implementations. Phase 2: safe stubs (log + no-op, sensible defaults).
   These are filled in across Phases 5-8 (dialogs, editor, diplomacy, reports).
***********************************************************************/
#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <cstdio>

#include "irrg_cxxside.h"
#include "irrg_dialogs.h"     /* Phase 6: city dialog state */
#include "gui_properties.h"   /* gui_properties (views the client supports) */

struct unit;   /* pointer-only use */
struct city;

/* ---- Flow / pages ---- */
void irrg_set_rulesets(int num_rulesets, char **rulesets) { (void)num_rulesets; (void)rulesets; }
void irrg_options_extra_init(void) {}
void irrg_server_connect(void) {}
void irrg_real_conn_list_dialog_update(void *unused) { (void)unused; }
void irrg_close_connection_dialog(void) {}
void irrg_sound_bell(void) {}
void irrg_real_set_client_page(enum client_pages page) { (void)page; }
enum client_pages irrg_get_current_client_page(void) { return PAGE_MAIN; }

/* ---- Map / units ---- */
void irrg_set_unit_icon(int idx, struct unit *punit) { (void)idx; (void)punit; }
void irrg_set_unit_icons_more_arrow(bool onoff) { (void)onoff; }
void irrg_real_focus_units_changed(void) {}

/* ---- Editor ---- */
void irrg_editgui_refresh(void) {}
void irrg_editgui_notify_object_created(int tag, int id) { (void)tag; (void)id; }
void irrg_editgui_notify_object_changed(int objtype, int object_id, bool removal)
{ (void)objtype; (void)object_id; (void)removal; }
void irrg_editgui_popup_properties(const struct tile_list *tiles, int objtype)
{ (void)tiles; (void)objtype; }
void irrg_editgui_tileset_changed(void) {}
void irrg_editgui_popdown_all(void) {}

/* ---- Gameplay dialogs ---- */
void irrg_popup_combat_info(int attacker_unit_id, int defender_unit_id,
                            int attacker_hp, int defender_hp,
                            bool make_att_veteran, bool make_def_veteran)
{ (void)make_att_veteran; (void)make_def_veteran;
  /* Full combat dialog (side-by-side unit art + damage) is a later phase; for
   * now surface the battle in the message window so it isn't silent. */
  char msg[160];
  std::snprintf(msg, sizeof(msg),
                "Combat: unit %d (hp %d) vs unit %d (hp %d).",
                attacker_unit_id, attacker_hp, defender_unit_id, defender_hp);
  irrg_message_append(msg);
  std::fprintf(stderr, "[irrg] %s\n", msg);
}
void irrg_update_timeout_label(void) {}
void irrg_start_turn(void) {}
void irrg_real_city_dialog_popup(struct city *pcity) { irrg_city_dialog_open(pcity); }
void irrg_real_city_dialog_refresh(struct city *pcity) { (void)pcity; }
void irrg_popdown_city_dialog(struct city *pcity) { (void)pcity; irrg_city_dialog_close(); }
void irrg_popdown_all_city_dialogs(void) { irrg_city_dialog_close(); }
bool irrg_handmade_scenario_warning(void) { return false; }
void irrg_refresh_unit_city_dialogs(struct unit *punit) { (void)punit; }
bool irrg_city_dialog_is_open(struct city *pcity) { return irrg_city_dialog_open_for(pcity); }
bool irrg_request_transport(struct unit *pcargo, struct tile *ptile)
{ (void)pcargo; (void)ptile; return false; }
void irrg_update_infra_dialog(void) {}

/* ---- Themes ---- */
void irrg_gui_load_theme(const char *directory, const char *theme_name)
{ (void)directory; (void)theme_name; }
void irrg_gui_clear_theme(void) {}
char **irrg_get_gui_specific_themes_directories(int *count)
{ if (count) *count = 0; return 0; }
char **irrg_get_usable_themes_in_directory(const char *directory, int *count)
{ (void)directory; if (count) *count = 0; return 0; }

/* ---- Diplomacy / treaties ---- */
void irrg_gui_init_meeting(struct treaty *ptreaty, struct player *they,
                           struct player *initiator)
{ (void)ptreaty; (void)they; (void)initiator; }
void irrg_gui_recv_cancel_meeting(struct treaty *ptreaty, struct player *they,
                                  struct player *initiator)
{ (void)ptreaty; (void)they; (void)initiator; }
void irrg_gui_prepare_clause_updt(struct treaty *ptreaty, struct player *they)
{ (void)ptreaty; (void)they; }
void irrg_gui_recv_create_clause(struct treaty *ptreaty, struct player *they)
{ (void)ptreaty; (void)they; }
void irrg_gui_recv_remove_clause(struct treaty *ptreaty, struct player *they)
{ (void)ptreaty; (void)they; }
void irrg_gui_recv_accept_treaty(struct treaty *ptreaty, struct player *they)
{ (void)ptreaty; (void)they; }

/* ---- Confirmation / reports / misc ---- */
void irrg_request_action_confirmation(const char *expl,
                                      struct act_confirmation_data *data)
{ (void)expl; (void)data; }
/* The *_update() hooks fire from the core update_queue on EVERY data change
 * (game start, unit focus, research, ...). They must only refresh an already-
 * open report, never open one -- otherwise any unit update force-opens the
 * report over the map (the "selecting a unit pops up the full Units report"
 * bug). The report body reads live client data each frame, so there is nothing
 * to do here. (The separate *_popup() hook is only fired for a human request.) */
void irrg_real_science_report_dialog_update(void *unused) { (void)unused; }
void irrg_science_report_dialog_redraw(void) {}
void irrg_science_report_dialog_popup(bool raise) { (void)raise; irrg_reports_open(1); }
void irrg_real_economy_report_dialog_update(void *unused) { (void)unused; }
void irrg_real_units_report_dialog_update(void *unused) { (void)unused; }
void irrg_endgame_report_dialog_start(const struct packet_endgame_report *packet)
{ (void)packet; }
void irrg_endgame_report_dialog_player(const struct packet_endgame_player *packet)
{ (void)packet; }
void irrg_popup_image(const char *tag) { (void)tag; }
void irrg_setup_gui_properties(void)
{
  /* Which tileset view types this client can render. Isometric + overhead
   * for now (Phase 5 adds the true 3D map view -> d3). */
  gui_properties.animations = false;
  gui_properties.views.isometric = true;
  gui_properties.views.overhead = true;
  gui_properties.views.d3 = false;
}
