MAINTAINERCLEANFILES = Makefile.in
MODULE = e-mod-tizen-keyrouter

LDFLAGS +=

pkgdir                 = $(libdir)/enlightenment/modules/$(MODULE)/$(MODULE_ARCH)
pkg_LTLIBRARIES        = module.la
if WAYLAND_ONLY
module_la_SOURCES      = e_mod_main_wl.c \
                         e_mod_main_wl.h \
                         keyrouter-protocol.c
module_la_CFLAGS       = @ENLIGHTENMENT_CFLAGS@ @WAYLAND_CFLAGS@ -DHAVE_WAYLAND_ONLY
module_la_LDFLAGS      = -module -avoid-version @WAYLAND_LIBS@ @ENLIGHTENMENT_LIBS@
else
module_la_SOURCES      = e_mod_main.c \
                         e_mod_main.h
module_la_LIBADD       =
module_la_CFLAGS       = @ENLIGHTENMENT_CFLAGS@
module_la_LDFLAGS      = -module -avoid-version @ENLIGHTENMENT_LIBS@
endif
module_la_DEPENDENCIES = $(top_builddir)/config.h

#uninstall:
#  rm -rf $(DESTDIR)$(libdir)/enlightenment/modules/$(MODULE)

if WAYLAND_ONLY
BUILT_SOURCES = keyrouter-protocol.c keyrouter-protocol.h

CLEANFILES = $(BUILT_SOURCES)

%-protocol.c : $(top_srcdir)/protocol/%.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(wayland_scanner) code < $< > $@

%-protocol.h : $(top_srcdir)/protocol/%.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(wayland_scanner) server-header < $< > $@

endif
