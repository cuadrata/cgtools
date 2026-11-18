/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* main.c - genesis of a CGHex application

   Copyright (C) 1998, 1999, 2000 Free Software Foundation

   CGHex is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   CGHex is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with CGHex. If not, see <https://www.gnu.org/licenses/>.

   Author: Jaka Mocnik <jaka.mocnik@kiss.uni-lj.si>
*/

#include <config.h>

#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>

#include "cghex.h"

static void usage(const gchar *argv0)
{
    printf(_("Usage: %s [OPTION...] [FILE...]\n"), argv0);
    printf("\n");
    printf(_("  --help       show this help and exit\n"));
    printf(_("  --version    show the version and exit\n"));
    printf("\n");
    printf(_("The usual GTK options, such as --display, are also understood.\n"));
}

int main(int argc, char **argv)
{
    gboolean options = TRUE;
    int i;

#ifdef ENABLE_NLS
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    textdomain(GETTEXT_PACKAGE);
#endif

    gtk_set_locale();

    /* these need no display */
    for(i = 1; i < argc && strcmp(argv[i], "--") != 0; i++) {
        if(strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        }
        else if(strcmp(argv[i], "--version") == 0) {
            printf("CGHex %s\n", PACKAGE_VERSION);
            return 0;
        }
    }

    gtk_init(&argc, &argv);
    /* every window's icon, where the window manager takes one in colour */
    gtk_window_set_default_icon_name("cghex");

    /* what is left of the options once GTK has taken its own */
    for(i = 1; i < argc && options; i++) {
        if(strcmp(argv[i], "--") == 0)
            options = FALSE;
        else if(argv[i][0] == '-' && argv[i][1] != 0) {
            fprintf(stderr, _("%s: unknown option %s\n"), argv[0], argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    /* load preferences */
    load_configuration();

    create_main_window();

    options = TRUE;
    for(i = 1; i < argc; i++) {
        if(options && strcmp(argv[i], "--") == 0) {
            options = FALSE;
            continue;
        }
        open_document(argv[i]);
    }

    /* and here we go... */
    gtk_main();

    return 0;
}
