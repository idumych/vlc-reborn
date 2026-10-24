guidir = $(pluginsdir)/gui
gui_LTLIBRARIES =

libncurses_plugin_la_SOURCES = gui/ncurses.c
libncurses_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(NCURSES_CFLAGS)
libncurses_plugin_la_LIBADD = $(NCURSES_LIBS) $(LIBPTHREAD) $(LIBM)
if HAVE_NCURSES
gui_LTLIBRARIES += libncurses_plugin.la
endif

EXTRA_DIST += gui/eject.c

libminimal_macosx_plugin_la_SOURCES = \
	gui/minimal_macosx/intf.h gui/minimal_macosx/intf.m \
	gui/minimal_macosx/misc.h gui/minimal_macosx/misc.m \
	gui/minimal_macosx/VLCMinimalVoutWindow.h gui/minimal_macosx/VLCMinimalVoutWindow.m \
	gui/minimal_macosx/macosx.c
libminimal_macosx_plugin_la_OBJCFLAGS = $(AM_OBJCFLAGS) -fobjc-arc -fobjc-exceptions
libminimal_macosx_plugin_la_LDFLAGS = $(AM_LDFLAGS) -rpath '$(guidir)' -Wl,-framework,Cocoa
EXTRA_LTLIBRARIES += libminimal_macosx_plugin.la
gui_LTLIBRARIES += $(LTLIBminimal_macosx)

libhaiku_plugin_la_SOURCES = \
	gui/haiku/haiku.cpp \
	gui/haiku/Bitmaps.h \
	gui/haiku/DrawingTidbits.cpp gui/haiku/DrawingTidbits.h \
	gui/haiku/InterfaceWindow.cpp gui/haiku/InterfaceWindow.h \
	gui/haiku/MediaControlView.cpp gui/haiku/MediaControlView.h \
	gui/haiku/MessagesWindow.cpp gui/haiku/MessagesWindow.h \
	gui/haiku/MsgVals.h \
	gui/haiku/PlayListWindow.cpp gui/haiku/PlayListWindow.h \
	gui/haiku/TransportButton.cpp gui/haiku/TransportButton.h
libhaiku_plugin_la_LIBADD = -lbe -ltracker
if HAVE_HAIKU
gui_LTLIBRARIES += libhaiku_plugin.la
endif

include gui/macosx/Makefile.am
include gui/qt/Makefile.am
include gui/skins2/Makefile.am
