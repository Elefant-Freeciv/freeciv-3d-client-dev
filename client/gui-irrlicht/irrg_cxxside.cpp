/***********************************************************************
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include "gui_interface.h"   /* struct gui_funcs, get_gui_funcs() */
#include "irrg_cxxside.h"

/*******************************************************************//**
  Fill the gui_funcs vtable with the irrg_* implementations.
  Every slot is assigned (none left NULL) so the C core never dereferences a
  dangling pointer.
***********************************************************************/
void setup_gui_funcs(void)
{
  struct gui_funcs *funcs = get_gui_funcs();

  funcs->ui_init = irrg_ui_init;
  funcs->ui_main = irrg_ui_main;
  funcs->ui_exit = irrg_ui_exit;

  funcs->get_gui_type = irrg_get_gui_type;
  funcs->insert_client_build_info = irrg_insert_client_build_info;

  funcs->version_message = irrg_version_message;
  funcs->real_output_window_append = irrg_real_output_window_append;

  funcs->tileset_type_set = irrg_tileset_type_set;
  funcs->load_gfxfile = irrg_load_gfxfile;
  funcs->load_gfxnumber = irrg_load_gfxnumber;
  funcs->create_sprite = irrg_create_sprite;
  funcs->get_sprite_dimensions = irrg_get_sprite_dimensions;
  funcs->crop_sprite = irrg_crop_sprite;
  funcs->free_sprite = irrg_free_sprite;

  funcs->color_alloc = irrg_color_alloc;
  funcs->color_free = irrg_color_free;

  funcs->canvas_create = irrg_canvas_create;
  funcs->canvas_free = irrg_canvas_free;
  funcs->canvas_set_zoom = irrg_canvas_set_zoom;
  funcs->has_zoom_support = irrg_has_zoom_support;
  funcs->canvas_mapview_init = irrg_canvas_mapview_init;
  funcs->canvas_copy = irrg_canvas_copy;
  funcs->canvas_put_sprite = irrg_canvas_put_sprite;
  funcs->canvas_put_sprite_full = irrg_canvas_put_sprite_full;
  funcs->canvas_put_sprite_full_scaled = irrg_canvas_put_sprite_full_scaled;
  funcs->canvas_put_sprite_fogged = irrg_canvas_put_sprite_fogged;
  funcs->canvas_put_rectangle = irrg_canvas_put_rectangle;
  funcs->canvas_fill_sprite_area = irrg_canvas_fill_sprite_area;
  funcs->canvas_put_line = irrg_canvas_put_line;
  funcs->canvas_put_curved_line = irrg_canvas_put_curved_line;
  funcs->get_text_size = irrg_get_text_size;
  funcs->canvas_put_text = irrg_canvas_put_text;
  funcs->map_canvas_size_refresh = irrg_map_canvas_size_refresh;

  funcs->set_rulesets = irrg_set_rulesets;
  funcs->options_extra_init = irrg_options_extra_init;
  funcs->server_connect = irrg_server_connect;
  funcs->add_net_input = irrg_add_net_input;
  funcs->remove_net_input = irrg_remove_net_input;
  funcs->real_conn_list_dialog_update = irrg_real_conn_list_dialog_update;
  funcs->close_connection_dialog = irrg_close_connection_dialog;
  funcs->add_idle_callback = irrg_add_idle_callback;
  funcs->sound_bell = irrg_sound_bell;
  funcs->real_set_client_page = irrg_real_set_client_page;
  funcs->get_current_client_page = irrg_get_current_client_page;

  funcs->set_unit_icon = irrg_set_unit_icon;
  funcs->set_unit_icons_more_arrow = irrg_set_unit_icons_more_arrow;
  funcs->real_focus_units_changed = irrg_real_focus_units_changed;
  funcs->gui_update_font = irrg_gui_update_font;

  funcs->editgui_refresh = irrg_editgui_refresh;
  funcs->editgui_notify_object_created = irrg_editgui_notify_object_created;
  funcs->editgui_notify_object_changed = irrg_editgui_notify_object_changed;
  funcs->editgui_popup_properties = irrg_editgui_popup_properties;
  funcs->editgui_tileset_changed = irrg_editgui_tileset_changed;
  funcs->editgui_popdown_all = irrg_editgui_popdown_all;

  funcs->popup_combat_info = irrg_popup_combat_info;
  funcs->update_timeout_label = irrg_update_timeout_label;
  funcs->start_turn = irrg_start_turn;
  funcs->real_city_dialog_popup = irrg_real_city_dialog_popup;
  funcs->real_city_dialog_refresh = irrg_real_city_dialog_refresh;
  funcs->popdown_city_dialog = irrg_popdown_city_dialog;
  funcs->popdown_all_city_dialogs = irrg_popdown_all_city_dialogs;
  funcs->handmade_scenario_warning = irrg_handmade_scenario_warning;
  funcs->refresh_unit_city_dialogs = irrg_refresh_unit_city_dialogs;
  funcs->city_dialog_is_open = irrg_city_dialog_is_open;
  funcs->request_transport = irrg_request_transport;
  funcs->update_infra_dialog = irrg_update_infra_dialog;

  funcs->gui_load_theme = irrg_gui_load_theme;
  funcs->gui_clear_theme = irrg_gui_clear_theme;
  funcs->get_gui_specific_themes_directories = irrg_get_gui_specific_themes_directories;
  funcs->get_usable_themes_in_directory = irrg_get_usable_themes_in_directory;

  funcs->gui_init_meeting = irrg_gui_init_meeting;
  funcs->gui_recv_cancel_meeting = irrg_gui_recv_cancel_meeting;
  funcs->gui_prepare_clause_updt = irrg_gui_prepare_clause_updt;
  funcs->gui_recv_create_clause = irrg_gui_recv_create_clause;
  funcs->gui_recv_remove_clause = irrg_gui_recv_remove_clause;
  funcs->gui_recv_accept_treaty = irrg_gui_recv_accept_treaty;

  funcs->request_action_confirmation = irrg_request_action_confirmation;
  funcs->real_science_report_dialog_update = irrg_real_science_report_dialog_update;
  funcs->science_report_dialog_redraw = irrg_science_report_dialog_redraw;
  funcs->science_report_dialog_popup = irrg_science_report_dialog_popup;
  funcs->real_economy_report_dialog_update = irrg_real_economy_report_dialog_update;
  funcs->real_units_report_dialog_update = irrg_real_units_report_dialog_update;
  funcs->endgame_report_dialog_start = irrg_endgame_report_dialog_start;
  funcs->endgame_report_dialog_player = irrg_endgame_report_dialog_player;

  funcs->popup_image = irrg_popup_image;
  funcs->setup_gui_properties = irrg_setup_gui_properties;
}
