/***********************************************************************
 Freeciv - gui-irrlicht: "common" GUI functions (declared in include/*_g.h,
   defined directly by the GUI, NOT part of the gui_funcs vtable).
   Phase 2: minimal stubs so the shared client core links. The real
   implementations (mapview, dialogs, menus, ...) land in Phases 5-8.
   Mirrors the minimal set provided by client/gui-stub.
***********************************************************************/
#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

extern "C" {
#include "gui_interface.h"
#include "unitlist.h"
#include "unit.h"
#include "city.h"
#include "mapview_common.h"
#include "dialogs_g.h"
#include "mapview_g.h"
#include "mapctrl_g.h"
#include "menu_g.h"
#include "messagedlg_g.h"
#include "optiondlg_g.h"
#include "connectdlg_g.h"
#include "pages_g.h"
#include "graphics_g.h"
#include "colors_g.h"
#include "sprite_g.h"
#include "citydlg_g.h"
#include "cityrep_g.h"
#include "repodlgs_g.h"
#include "plrdlg_g.h"
#include "diplodlg_g.h"
#include "inteldlg_g.h"
#include "luaconsole_g.h"
#include "spaceshipdlg_g.h"
#include "voteinfo_bar_g.h"
#include "wldlg_g.h"
#include "finddlg_g.h"
#include "gotodlg_g.h"
#include "helpdlg_g.h"
#include "ratesdlg_g.h"
#include "chatline_g.h"
#include "themes_g.h"
#include "editgui_g.h"
#include "gui_main_g.h"

/* ---- extern variables (GUI identity / text) ---- */
extern const char *client_string = "irrlicht";
extern const char * const gui_character_encoding = "UTF-8";
extern const bool gui_use_transliteration = false;

/* ---- function stubs (generated; signatures from include/*_g.h) ---- */
int action_selection_actor_unit(void)
{
  return 0;
}

void action_selection_close(void)
{
}

void action_selection_no_longer_in_progress_gui_specific(int actor_unit_id)
{
}

void action_selection_refresh(struct unit *actor_unit, struct city *target_city, struct unit *target_unit, struct tile *target_tile, struct extra_type *target_extra, const struct act_prob *act_probs)
{
}

int action_selection_target_city(void)
{
  return 0;
}

int action_selection_target_extra(void)
{
  return 0;
}

int action_selection_target_tile(void)
{
  return 0;
}

int action_selection_target_unit(void)
{
  return 0;
}

void close_all_diplomacy_dialogs(void)
{
}

void close_intel_dialog(struct player *p)
{
}

int color_brightness_score(struct color *color)
{
  return 0;
}

void create_line_at_mouse_pos(void)
{
}

void dirty_all(void)
{
}

void dirty_rect(int canvas_x, int canvas_y, int pixel_width, int pixel_height)
{
}

void draw_selection_rectangle(int canvas_x, int canvas_y, int w, int h)
{
}

void flush_dirty(void)
{
}

void get_overview_area_dimensions(int *width, int *height)
{
}

struct canvas * get_overview_window(void)
{
  return 0;
}

const char ** gfx_fileextensions(void)
{
  static const char *e[]={"png","jpg","jpeg","bmp","svg",0}; return e;
}

void gui_flush(void)
{
}

void handle_authentication_req(enum authentication_type type, const char *message)
{
}

void handle_game_load(bool load_successful, const char *load_filename)
{
}

void hilite_cities_from_canvas(void)
{
}

bool meswin_dialog_is_open(void)
{
  return false;
}

void meswin_dialog_popup(bool raise)
{
}

void option_dialog_popdown(const struct option_set *poptset)
{
}

void option_gui_add(struct option *poption)
{
}

void option_gui_remove(struct option *poption)
{
}

void option_gui_update(struct option *poption)
{
}

void overview_size_changed(void)
{
}

void popdown_all_game_dialogs(void)
{
}

void popdown_help_dialog(void)
{
}

void popdown_races_dialog(void)
{
}

void popup_action_selection(struct unit *actor_unit, struct city *target_city, struct unit *target_unit, struct tile *target_tile, struct extra_type *target_extra, const struct act_prob *act_probs)
{
}

void popup_bribe_stack_dialog(struct unit *actor, struct tile *ptile, int cost, const struct action *paction)
{
}

void popup_bribe_unit_dialog(struct unit *actor, struct unit *punit, int cost, const struct action *paction)
{
}

void popup_connect_msg(const char *headline, const char *message)
{
}

void popup_incite_dialog(struct unit *actor, struct city *pcity, int cost, const struct action *paction)
{
}

void popup_musicset_suggestion_dialog(void)
{
}

void popup_newcity_dialog(struct unit *punit, const char *suggestname)
{
}

void popup_notify_dialog(const char *caption, const char *headline, const char *lines)
{
}

void popup_notify_goto_dialog(const char *headline, const char *lines, const struct text_tag_list *tags, struct tile *ptile)
{
}

void popup_pillage_dialog(struct unit *punit, bv_extras extras)
{
}

void popup_sabotage_dialog(struct unit *actor, struct city *pcity, const struct action *paction)
{
}

void popup_soundset_suggestion_dialog(void)
{
}

bool popup_theme_suggestion_dialog(const char *theme_name)
{
  return false;
}

void popup_tileset_suggestion_dialog(void)
{
}

void put_cross_overlay_tile(struct tile *ptile)
{
}

void races_toggles_set_sensitive(void)
{
}

void races_update_pickable(bool nationset_change)
{
}

void real_city_report_dialog_update(void *unused)
{
}

void real_city_report_update_city(struct city *pcity)
{
}

void real_luaconsole_append(const char *astring, const struct text_tag_list *tags)
{
}

void real_menus_init(void)
{
}

void real_menus_update(void)
{
}

void real_meswin_dialog_update(void *unused)
{
}

void real_multipliers_dialog_update(void *unused)
{
}

void real_players_dialog_update(void*)
{
}

void refresh_spaceship_dialog(struct player *pplayer)
{
}

void set_turn_done_button_state(bool state)
{
}

void show_tech_gained_dialog(Tech_type_id tech)
{
}

void show_tileset_error(bool fatal, const char *tset_name, const char *msg)
{
}

void tileset_changed(void)
{
}

void toggle_city_hilite(struct city *pcity, bool on_off)
{
}

void unit_select_dialog_popup(struct tile *ptile)
{
}

void unit_select_dialog_update_real(void *unused)
{
}

void update_city_descriptions(void)
{
}

void update_info_label(void)
{
}

void update_intel_dialog(struct player *p)
{
}

void update_map_canvas_scrollbars(void)
{
}

void update_map_canvas_scrollbars_size(void)
{
}

void update_mouse_cursor(enum cursor_type new_cursor_type)
{
}

void update_overview_scroll_window_pos(int x, int y)
{
}

void update_rect_at_mouse_pos(void)
{
}

void update_start_page(void)
{
}

void update_turn_done_button(bool do_restore)
{
}

void update_unit_info_label(struct unit_list *punitlist)
{
}

void voteinfo_gui_update(void)
{
}

}   /* extern "C" */
