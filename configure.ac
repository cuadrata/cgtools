dnl Process this file with autoreconf -vif to produce configure.

AC_PREREQ([2.71])
AC_INIT([cgtools], [1.4.0], [], [cgtools])
AC_CONFIG_SRCDIR([cghex/gtkhex.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.16 foreign no-define subdir-objects])

AC_PROG_CC

dnl Build as ISO C99 unless the user chose a language standard, as the
dnl toolkit does. POSIX interfaces are requested the standard way, with
dnl _XOPEN_SOURCE=600 (SUSv3, aligned with C99).
case " $CC $CFLAGS " in
*" -std="*) ;;
*)
  AC_MSG_CHECKING([whether $CC accepts -std=c99])
  tools_save_CC="$CC"
  CC="$CC -std=c99"
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [return 0;])],
    [AC_MSG_RESULT(yes)], [AC_MSG_RESULT(no); CC="$tools_save_CC"])
  ;;
esac
case " $CC $CPPFLAGS $CFLAGS " in
*" -D_XOPEN_SOURCE"* | *" -D_POSIX_C_SOURCE"*) ;;
*) CPPFLAGS="$CPPFLAGS -D_XOPEN_SOURCE=600" ;;
esac

if test "x$GCC" = "xyes"; then
  case " $CFLAGS " in
  *@<:@\ \	@:>@-Wall@<:@\ \	@:>@*) ;;
  *) CFLAGS="$CFLAGS -Wall" ;;
  esac
fi

AC_PROG_INSTALL
AC_PROG_MAKE_SET
PKG_PROG_PKG_CONFIG

dnl The maths library, for cgcalc only.
tools_save_LIBS="$LIBS"
LIBS=
AC_SEARCH_LIBS([pow], [m])
MATH_LIBS="$LIBS"
LIBS="$tools_save_LIBS"
AC_SUBST([MATH_LIBS])

dnl The toolkit.
PKG_CHECK_MODULES([GTK], [gtk+-1.4 >= 1.4.0])

dnl cgrun reads .desktop files and starts programs with what CGTK's
dnl GLib has had since key files and g_spawn_async() came to it. The
dnl version did not change with them, so they are looked for.
tools_save_LIBS="$LIBS"
LIBS="$GTK_LIBS $LIBS"
AC_CHECK_FUNCS([g_key_file_new g_spawn_async], [],
  [AC_MSG_ERROR([this CGTK is too old: its GLib has no key files or g_spawn_async()])])
dnl cghex and cgcalc give their windows the icons they are installed
dnl with, which CGTK finds for them.
AC_CHECK_FUNC([gtk_window_set_default_icon_name], [],
  [AC_MSG_ERROR([this CGTK is too old: it has no gtk_window_set_default_icon_name()])])
LIBS="$tools_save_LIBS"

dnl Translations, from the system's gettext. Each program keeps its own
dnl catalogues and text domain, in cghex/po, cgcalc/po and cgrun/po.
AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION([0.19.6])
AM_GNU_GETTEXT_REQUIRE_VERSION([0.19.6])

AC_CONFIG_FILES([
Makefile
cghex/po/Makefile.in
cgcalc/po/Makefile.in
cgrun/po/Makefile.in
])
AC_OUTPUT
