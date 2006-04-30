SUBDIRS = rfb

if BOXTYPE_DREAMBOX
AM_CPPFLAGS = @FREETYPE_CFLAGS@ -I./rfb  -I$(top_srcdir)/include
vdr_la_LDFLAGS = -rpath $(PLUGINDIR) -module -avoid-version @FREETYPE_LIBS@
else
AM_CPPFLAGS = -I./rfb @FREETYPE_CFLAGS@ @CONFIGFILE_CFLAGS@ -I$(top_srcdir)/include -I../../../dvb/zapit/include -I../../neutrino/lib
vdr_la_LDFLAGS = -rpath $(PLUGINDIR) -module -avoid-version @FREETYPE_LIBS@ @CONFIGFILE_LIBS@

endif
noinst_LTLIBRARIES = vdr.la

vdr_la_SOURCES = args.c fbdraw.c fscale.c fbgl.c keyboard.c \
                 list.c overlay.c rfbproto.c sockets.c vdrviewer.cpp ringbuffer.c viewermenu.cpp


vdr_la_LIBADD = rfb/librfb.a 

install-exec-local:
	install -d $(DESTDIR)$(PLUGINDIR)
	install -d $(DESTDIR)$(CONFIGDIR)
	$(LIBTOOL) --mode=install install vdr.la $(DESTDIR)$(PLUGINDIR)
	install -m 0644 $(srcdir)/vdr.cfg $(DESTDIR)$(PLUGINDIR)
	if test ! -e  $(DESTDIR)$(CONFIGDIR)/vdr.conf; then cp $(srcdir)/vdr.conf $(DESTDIR)$(CONFIGDIR);fi
