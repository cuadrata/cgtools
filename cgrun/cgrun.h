/* cgrun.h - what the parts of cgrun share

   Copyright (C) 2026 The CGTK authors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program. If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef CGRUN_H
#define CGRUN_H

#include <glib.h>

#ifdef ENABLE_NLS
#include <libintl.h>
#define _(String) gettext(String)
#else
#define _(String) (String)
#define textdomain(String) (String)
#define bindtextdomain(Domain,Directory) (Domain)
#endif

/* A program the desktop knows of: one .desktop file, read. */
typedef struct {
    gchar *id;      /* the file's name among its kind, as "org.x.Foo.desktop" */
    gchar *file;    /* where it is */
    gchar *name;    /* in the user's language */
    gchar *about;   /* a few words on it, or NULL */
    gchar *exec;    /* its command line, field codes and all */
    gchar *dir;     /* where it is to run, or NULL */
    gchar *key;     /* its name as it is searched */
    gchar *more;    /* what else of it is searched */
    gboolean terminal;
} App;

/* apps.c */
GList *apps_load(void);
void apps_free(GList *apps);
gchar **app_argv(const App *app, GError **error);
gchar *text_key(const gchar *text);
gint text_match(const gchar *key, const gchar *more, const gchar *want);
gchar **line_argv(const gchar *line, GError **error);
gboolean run_argv(gchar **argv, const gchar *dir, gboolean terminal, GError **error);

#endif /* CGRUN_H */
