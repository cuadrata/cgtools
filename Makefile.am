## Process this file with automake to produce Makefile.in.

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = cghex/po cgcalc/po cgrun/po

AM_CPPFLAGS = \
        -DLOCALEDIR=\"$(localedir)\" \
        $(GTK_CFLAGS)

bin_PROGRAMS = cghex/cghex cgcalc/cgcalc cgrun/cgrun
man_MANS = cghex/cghex.1 cgcalc/cgcalc.1 cgrun/cgrun.1

desktopdir = $(datadir)/applications
dist_desktop_DATA = cghex/cghex.desktop cgcalc/cgcalc.desktop

# The icons: each tool's one drawing, the sizes of it rendered beforehand
# by render-icons.sh, and an XPM, which is in the tool too, for whatever
# still looks in pixmaps.
icontools = cghex cgcalc
iconsizes = 16 22 24 32 48 64 128 256
hicolordir = $(datadir)/icons/hicolor
scalabledir = $(hicolordir)/scalable/apps
dist_scalable_DATA = cghex/icons/cghex.svg cgcalc/icons/cgcalc.svg
pixmapsdir = $(datadir)/pixmaps
dist_pixmaps_DATA = cghex/icons/cghex.xpm cgcalc/icons/cgcalc.xpm

install-data-local:
	for t in $(icontools); do for s in $(iconsizes); do \
		$(MKDIR_P) "$(DESTDIR)$(hicolordir)/$${s}x$${s}/apps" && \
		$(INSTALL_DATA) "$(srcdir)/$$t/icons/$$t-$$s.png" \
			"$(DESTDIR)$(hicolordir)/$${s}x$${s}/apps/$$t.png" || exit 1; \
	done; done

uninstall-local:
	for t in $(icontools); do for s in $(iconsizes); do \
		rm -f "$(DESTDIR)$(hicolordir)/$${s}x$${s}/apps/$$t.png"; \
	done; done

EXTRA_DIST = $(man_MANS) history \
        cghex/README cgcalc/README \
        cgrun/README \
        render-icons.sh \
        cghex/icons/cghex-16.png cghex/icons/cghex-22.png cghex/icons/cghex-24.png cghex/icons/cghex-32.png \
        cghex/icons/cghex-48.png cghex/icons/cghex-64.png cghex/icons/cghex-128.png cghex/icons/cghex-256.png \
        cgcalc/icons/cgcalc-16.png cgcalc/icons/cgcalc-22.png cgcalc/icons/cgcalc-24.png cgcalc/icons/cgcalc-32.png \
        cgcalc/icons/cgcalc-48.png cgcalc/icons/cgcalc-64.png cgcalc/icons/cgcalc-128.png cgcalc/icons/cgcalc-256.png

# cghex

cghex_cghex_SOURCES = \
        cghex/chartable.c \
        cghex/config.c \
        cghex/converter.c \
        cghex/findreplace.c \
        cghex/cghex.h \
        cghex/gtkhex.c \
        cghex/gtkhex.h \
        cghex/hex-document-ui.c \
        cghex/hex-document.c \
        cghex/hex-document.h \
        cghex/main.c \
        cghex/preferences.c \
        cghex/ui.c \
        cghex/window.c
cghex_cghex_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/cghex \
        -DGETTEXT_PACKAGE=\"cghex\"
cghex_cghex_LDADD = $(GTK_LIBS) $(LIBINTL)

# cghex's document without a window: make check edits one as the editor
# does and looks at what it then holds.
check_PROGRAMS = tests/cghex-document
tests_cghex_document_SOURCES = \
        tests/cghex-document.c \
        cghex/cghex.h \
        cghex/gtkhex.h \
        cghex/hex-document.c \
        cghex/hex-document.h
tests_cghex_document_CPPFLAGS = $(cghex_cghex_CPPFLAGS)
tests_cghex_document_LDADD = $(GTK_LIBS) $(LIBINTL)
TESTS = tests/cghex-document
CLEANFILES = cghex-document.tmp

# cgcalc

cgcalc_cgcalc_SOURCES = \
        cgcalc/gtkcalc.c \
        cgcalc/gtkcalc.h \
        cgcalc/cgcalc.c \
        cgcalc/cgcalc.h \
        cgcalc/sr.c \
        cgcalc/sr.h
cgcalc_cgcalc_CPPFLAGS = $(AM_CPPFLAGS) -DGETTEXT_PACKAGE=\"cgcalc\"
cgcalc_cgcalc_LDADD = $(GTK_LIBS) $(LIBINTL) $(MATH_LIBS)

# cgrun

cgrun_cgrun_SOURCES = \
        cgrun/apps.c \
        cgrun/cgrun.c \
        cgrun/cgrun.h
cgrun_cgrun_CPPFLAGS = $(AM_CPPFLAGS) -DGETTEXT_PACKAGE=\"cgrun\"
cgrun_cgrun_LDADD = $(GTK_LIBS) $(LIBINTL)

# What cgrun does without a window: make check has it read the .desktop
# files in tests/data, match names, and start programs.
check_PROGRAMS += tests/cgrun-apps
tests_cgrun_apps_SOURCES = \
        tests/cgrun-apps.c \
        cgrun/apps.c \
        cgrun/cgrun.h
tests_cgrun_apps_CPPFLAGS = $(cgrun_cgrun_CPPFLAGS) -I$(top_srcdir)/cgrun \
        -DTESTDATA=\"$(abs_top_srcdir)/tests/data\"
tests_cgrun_apps_LDADD = $(GTK_LIBS) $(LIBINTL)
TESTS += tests/cgrun-apps
CLEANFILES += cgrun-apps.tmp
EXTRA_DIST += \
        tests/data/system/applications/broken.desktop \
        tests/data/system/applications/editor.desktop \
        tests/data/system/applications/gone.desktop \
        tests/data/system/applications/here.desktop \
        tests/data/system/applications/hidden.desktop \
        tests/data/system/applications/link.desktop \
        tests/data/system/applications/mine.desktop \
        tests/data/system/applications/noexec.desktop \
        tests/data/system/applications/notes.txt \
        tests/data/system/applications/nothere.desktop \
        tests/data/system/applications/other.desktop \
        tests/data/system/applications/sub/inner.desktop \
        tests/data/system/applications/tryless.desktop \
        tests/data/user/applications/gone.desktop \
        tests/data/user/applications/mine.desktop
