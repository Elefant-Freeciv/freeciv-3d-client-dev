/*
 * gui-irrlicht: C++ side declarations (mirrors gui-qt/qtg_cxxside.h).
 *
 * These are the C++ implementations of the gui_funcs vtable. They are only
 * referenced from within gui-irrlicht (via setup_gui_funcs in irrg_cxxside.cpp);
 * the C client core never names them directly (it calls the unprefixed
 * forwarders in gui_interface.c, which dispatch through the vtable). Hence no
 * extern "C" is needed.
 */
#ifndef IRG_CXXSIDE_H
#define IRG_CXXSIDE_H

#include "gui_interface.h"   /* struct gui_funcs, get_gui_funcs(), shared types */

/*******************************************************************//**
  Fill the gui_funcs vtable with the irrg_* implementations. Called from
  irrg_ui_main() before the device is created.
***********************************************************************/
void setup_gui_funcs(void);

/* ---- Lifecycle ---- */
void irrg_ui_init(void);
int  irrg_ui_main(int argc, char *argv[]);
void irrg_ui_exit(void);
enum gui_type irrg_get_gui_type(void);
void irrg_insert_client_build_info(char *outbuf, size_t outlen);
void irrg_version_message(const char *vertext);
void irrg_real_output_window_append(const char *astring,
                                    const struct text_tag_list *tags, int conn_id);

/* ---- Graphics primitives ---- */
void irrg_tileset_type_set(enum ts_type type);
struct sprite *irrg_load_gfxfile(const char *filename, bool svgflag);
struct sprite *irrg_load_gfxnumber(int num);
struct sprite *irrg_create_sprite(int width, int height, struct color *pcolor);
void irrg_get_sprite_dimensions(struct sprite *sprite, int *width, int *height);
struct sprite *irrg_crop_sprite(struct sprite *source, int x, int y,
                                int width, int height, struct sprite *mask,
                                int mask_offset_x, int mask_offset_y,
                                float scale, bool smooth);
void irrg_free_sprite(struct sprite *s);
struct color *irrg_color_alloc(int r, int g, int b);
void irrg_color_free(struct color *pcolor);

/* ---- Canvas ---- */
struct canvas *irrg_canvas_create(int width, int height);
void irrg_canvas_free(struct canvas *store);
void irrg_canvas_set_zoom(struct canvas *store, float zoom);
bool irrg_has_zoom_support(void);
void irrg_canvas_mapview_init(struct canvas *store);
void irrg_canvas_copy(struct canvas *dest, struct canvas *src,
                      int src_x, int src_y, int dest_x, int dest_y,
                      int width, int height);
void irrg_canvas_put_sprite(struct canvas *pcanvas, int canvas_x, int canvas_y,
                            struct sprite *psprite, int offset_x, int offset_y,
                            int width, int height);
void irrg_canvas_put_sprite_full(struct canvas *pcanvas, int canvas_x,
                                 int canvas_y, struct sprite *psprite);
void irrg_canvas_put_sprite_full_scaled(struct canvas *pcanvas, int canvas_x,
                                        int canvas_y, int canvas_w, int canvas_h,
                                        struct sprite *psprite);
void irrg_canvas_put_sprite_fogged(struct canvas *pcanvas, int canvas_x,
                                   int canvas_y, struct sprite *psprite,
                                   bool fog, int fog_x, int fog_y);
void irrg_canvas_put_rectangle(struct canvas *pcanvas, struct color *pcolor,
                               int canvas_x, int canvas_y, int width, int height);
void irrg_canvas_fill_sprite_area(struct canvas *pcanvas, struct sprite *psprite,
                                  struct color *pcolor, int canvas_x, int canvas_y);
void irrg_canvas_put_line(struct canvas *pcanvas, struct color *pcolor,
                          enum line_type ltype, int start_x, int start_y,
                          int dx, int dy);
void irrg_canvas_put_curved_line(struct canvas *pcanvas, struct color *pcolor,
                                 enum line_type ltype, int start_x, int start_y,
                                 int dx, int dy);
void irrg_get_text_size(int *width, int *height, enum client_font font,
                        const char *text);
void irrg_canvas_put_text(struct canvas *pcanvas, int canvas_x, int canvas_y,
                          enum client_font font, struct color *pcolor,
                          const char *text);
void irrg_map_canvas_size_refresh(void);

/* ---- Event loop / IO / flow ---- */
void irrg_set_rulesets(int num_rulesets, char **rulesets);
void irrg_options_extra_init(void);
void irrg_server_connect(void);
void irrg_add_net_input(int sock);
void irrg_remove_net_input(void);
void irrg_real_conn_list_dialog_update(void *unused);
void irrg_close_connection_dialog(void);
void irrg_add_idle_callback(void (callback)(void *), void *data);
void irrg_sound_bell(void);
void irrg_real_set_client_page(enum client_pages page);
enum client_pages irrg_get_current_client_page(void);

/* ---- Map / units / font ---- */
void irrg_set_unit_icon(int idx, struct unit *punit);
void irrg_set_unit_icons_more_arrow(bool onoff);
void irrg_real_focus_units_changed(void);
void irrg_gui_update_font(const char *font_name, const char *font_value);

/* ---- Editor ---- */
void irrg_editgui_refresh(void);
void irrg_editgui_notify_object_created(int tag, int id);
void irrg_editgui_notify_object_changed(int objtype, int object_id, bool removal);
void irrg_editgui_popup_properties(const struct tile_list *tiles, int objtype);
void irrg_editgui_tileset_changed(void);
void irrg_editgui_popdown_all(void);

/* ---- Dialogs / gameplay ---- */
void irrg_popup_combat_info(int attacker_unit_id, int defender_unit_id,
                            int attacker_hp, int defender_hp,
                            bool make_att_veteran, bool make_def_veteran);
void irrg_update_timeout_label(void);
void irrg_start_turn(void);
void irrg_real_city_dialog_popup(struct city *pcity);
void irrg_real_city_dialog_refresh(struct city *pcity);
void irrg_popdown_city_dialog(struct city *pcity);
void irrg_popdown_all_city_dialogs(void);
bool irrg_handmade_scenario_warning(void);
void irrg_refresh_unit_city_dialogs(struct unit *punit);
bool irrg_city_dialog_is_open(struct city *pcity);
bool irrg_request_transport(struct unit *pcargo, struct tile *ptile);
void irrg_update_infra_dialog(void);

/* ---- Themes ---- */
void irrg_gui_load_theme(const char *directory, const char *theme_name);
void irrg_gui_clear_theme(void);
char **irrg_get_gui_specific_themes_directories(int *count);
char **irrg_get_usable_themes_in_directory(const char *directory, int *count);

/* ---- Diplomacy / treaties ---- */
void irrg_gui_init_meeting(struct treaty *ptreaty, struct player *they,
                           struct player *initiator);
void irrg_gui_recv_cancel_meeting(struct treaty *ptreaty, struct player *they,
                                  struct player *initiator);
void irrg_gui_prepare_clause_updt(struct treaty *ptreaty, struct player *they);
void irrg_gui_recv_create_clause(struct treaty *ptreaty, struct player *they);
void irrg_gui_recv_remove_clause(struct treaty *ptreaty, struct player *they);
void irrg_gui_recv_accept_treaty(struct treaty *ptreaty, struct player *they);

/* ---- Confirmation / reports / misc ---- */
void irrg_request_action_confirmation(const char *expl,
                                      struct act_confirmation_data *data);
void irrg_real_science_report_dialog_update(void *unused);
void irrg_science_report_dialog_redraw(void);
void irrg_science_report_dialog_popup(bool raise);
void irrg_real_economy_report_dialog_update(void *unused);
void irrg_real_units_report_dialog_update(void *unused);
void irrg_endgame_report_dialog_start(const struct packet_endgame_report *packet);
void irrg_endgame_report_dialog_player(const struct packet_endgame_player *packet);
void irrg_popup_image(const char *tag);
void irrg_setup_gui_properties(void);

#endif /* IRG_CXXSIDE_H */
