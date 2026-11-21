## Process this file with automake to produce Makefile.in.

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = cghex/po cgcalc/po cgrun/po cgpaint/po cgedit/po

AM_CPPFLAGS = \
        -DLOCALEDIR=\"$(localedir)\" \
        $(GTK_CFLAGS)

bin_PROGRAMS = cghex/cghex cgcalc/cgcalc cgrun/cgrun cgpaint/cgpaint cgedit/cgedit
man_MANS = cghex/cghex.1 cgcalc/cgcalc.1 cgrun/cgrun.1 cgpaint/cgpaint.1 cgedit/cgedit.1

desktopdir = $(datadir)/applications
dist_desktop_DATA = cghex/cghex.desktop cgcalc/cgcalc.desktop cgpaint/cgpaint.desktop \
        cgedit/cgedit.desktop

# The icons: each tool's one drawing, the sizes of it rendered beforehand
# by render-icons.sh, and an XPM, which is in the tool too, for whatever
# still looks in pixmaps.
icontools = cghex cgcalc cgpaint cgedit
iconsizes = 16 22 24 32 48 64 128 256
hicolordir = $(datadir)/icons/hicolor
scalabledir = $(hicolordir)/scalable/apps
dist_scalable_DATA = cghex/icons/cghex.svg cgcalc/icons/cgcalc.svg cgpaint/icons/cgpaint.svg \
        cgedit/icons/cgedit.svg
pixmapsdir = $(datadir)/pixmaps
dist_pixmaps_DATA = cghex/icons/cghex.xpm cgcalc/icons/cgcalc.xpm cgpaint/icons/cgpaint.xpm \
        cgedit/icons/cgedit.xpm

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
        cgrun/README cgpaint/README cgedit/README \
        render-icons.sh \
        cghex/icons/cghex-16.png cghex/icons/cghex-22.png cghex/icons/cghex-24.png cghex/icons/cghex-32.png \
        cghex/icons/cghex-48.png cghex/icons/cghex-64.png cghex/icons/cghex-128.png cghex/icons/cghex-256.png \
        cgcalc/icons/cgcalc-16.png cgcalc/icons/cgcalc-22.png cgcalc/icons/cgcalc-24.png cgcalc/icons/cgcalc-32.png \
        cgcalc/icons/cgcalc-48.png cgcalc/icons/cgcalc-64.png cgcalc/icons/cgcalc-128.png cgcalc/icons/cgcalc-256.png \
        cgpaint/icons/cgpaint-16.png cgpaint/icons/cgpaint-22.png cgpaint/icons/cgpaint-24.png cgpaint/icons/cgpaint-32.png \
        cgpaint/icons/cgpaint-48.png cgpaint/icons/cgpaint-64.png cgpaint/icons/cgpaint-128.png cgpaint/icons/cgpaint-256.png \
        cgedit/icons/cgedit-16.png cgedit/icons/cgedit-22.png cgedit/icons/cgedit-24.png cgedit/icons/cgedit-32.png \
        cgedit/icons/cgedit-48.png cgedit/icons/cgedit-64.png cgedit/icons/cgedit-128.png cgedit/icons/cgedit-256.png

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

# cgpaint

cgpaint_cgpaint_SOURCES = \
        cgpaint/cgpaint.c \
        cgpaint/cgpaint.h \
        cgpaint/file.c \
        cgpaint/image.c \
        cgpaint/pictures.h \
        cgpaint/view.c
cgpaint_cgpaint_CPPFLAGS = $(AM_CPPFLAGS) $(PNG_CFLAGS) -I$(top_srcdir)/cgpaint -DGETTEXT_PACKAGE=\"cgpaint\"
cgpaint_cgpaint_LDADD = $(GTK_LIBS) $(PNG_LIBS) $(LIBINTL) $(MATH_LIBS)

# What cgpaint draws and the files it keeps, without a window: make
# check draws lines and shapes and looks at the points, and saves a
# picture as each kind of file and reads it again.
check_PROGRAMS += tests/cgpaint-image
tests_cgpaint_image_SOURCES = \
        tests/cgpaint-image.c \
        cgpaint/cgpaint.h \
        cgpaint/file.c \
        cgpaint/image.c
tests_cgpaint_image_CPPFLAGS = $(cgpaint_cgpaint_CPPFLAGS)
tests_cgpaint_image_LDADD = $(cgpaint_cgpaint_LDADD)
TESTS += tests/cgpaint-image
CLEANFILES += cgpaint-image.tmp cgpaint-image.tmp.bmp cgpaint-image.tmp.xpm \
        cgpaint-image.tmp.png "1 cgpaint-image.tmp.xpm"

# cgedit

cgedit_cgedit_SOURCES = \
        cgedit/cgedit.c \
        cgedit/cgedit.h \
        cgedit/encoding.c \
        cgedit/encoding.h \
        cgedit/gtkeditor.c \
        cgedit/gtkeditor.h \
        cgedit/textbuf.c \
        cgedit/textbuf.h
cgedit_cgedit_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/cgedit -DGETTEXT_PACKAGE=\"cgedit\"
cgedit_cgedit_LDADD = $(GTK_LIBS) $(LIBINTL)

# The text cgedit edits, without a window: make check changes one at
# random beside a plain copy of it and looks that the two agree, undoes
# and redoes it all, and finds and replaces.
check_PROGRAMS += tests/cgedit-text
tests_cgedit_text_SOURCES = \
        tests/cgedit-text.c \
        cgedit/textbuf.c \
        cgedit/textbuf.h
tests_cgedit_text_CPPFLAGS = $(cgedit_cgedit_CPPFLAGS)
tests_cgedit_text_LDADD = $(GTK_LIBS) $(LIBINTL)
TESTS += tests/cgedit-text
