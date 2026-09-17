## Process this file with automake to produce Makefile.in

include $(top_srcdir)/bootstrap/Makerules.mk

noinst_LTLIBRARIES = libgui-irrlicht.la
AM_CPPFLAGS = \
	-I. \
	-I$(srcdir)/.. \
	-I$(srcdir)/../include \
	-I$(top_srcdir)/utility \
	-I$(top_srcdir)/common \
	-I$(top_srcdir)/common/scriptcore \
	-I$(top_srcdir)/common/aicore \
	-I$(top_srcdir)/common/networking \
	-I$(srcdir)/../luascript \
	-I$(srcdir)/../agents \
	-I$(top_srcdir)/dependencies/tinycthread \
	-I$(top_srcdir)/gen_headers/enums \
	$(gui_irrlicht_cppflags) $(SOUND_CFLAGS)

AM_CXXFLAGS = $(gui_irrlicht_cxxflags)

# Phase 1: skeleton. Phases 2-6 add the real sources (irrg_cxxside, graphics,
# canvas, sprite, color, mapview, mapctrl, dialogs, ...).
libgui_irrlicht_la_SOURCES = \
	gui_main.cpp \
	gui_main.h \
	irrg_cxxside.cpp \
	irrg_cxxside.h \
	graphics.h \
	irrg_bfont.h \
	irrg_graphics.cpp \
	irrg_map3d.cpp \
	irrg_map3d.h \
	irrg_interact.cpp \
	irrg_interact.h \
	irrg_dialogs.cpp \
	irrg_dialogs.h \
	irrg_unitbar.cpp \
	irrg_unitbar.h \
	irrg_menu.cpp \
	irrg_menu.h \
	irrg_settings.cpp \
	irrg_settings.h \
	irrg_newgame.cpp \
	irrg_newgame.h \
	irrg_gamemenu.cpp \
	irrg_gamemenu.h \
	irrg_theme.cpp \
	irrg_theme.h \
	irrg_research.cpp \
	irrg_research.h \
	irrg_stubs.cpp \
	irrg_common_stubs.cpp

libgui_irrlicht_la_LIBADD = $(gui_irrlicht_libs)
