/* cgedit.h - what the parts of cgedit share

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

#ifndef CGEDIT_H
#define CGEDIT_H

#include <gtk/gtk.h>

#ifdef ENABLE_NLS
#include <libintl.h>
#define _(String) gettext(String)
#else
#define _(String) (String)
#define textdomain(String) (String)
#define bindtextdomain(Domain,Directory) (Domain)
#endif
#define N_(String) (String)

#include "gtkeditor.h"

/* What is kept from one time to the next, in cgeditrc. */
typedef struct {
    gint width, height;
    gchar *font;       /* its name, or NULL for the theme's */
    gboolean wrap, numbers, indent;
} Settings;

extern Settings settings;

/* cgedit.c */
void tell(GtkWindow *over, const gchar *text);

#endif /* CGEDIT_H */
