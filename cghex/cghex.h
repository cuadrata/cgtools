/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* cghex.h - defines CGHex ;)

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

#ifndef CGHEX_H
#define CGHEX_H

#include <stdio.h>

#include <gtk/gtk.h>

#include "hex-document.h"
#include "gtkhex.h"

#ifdef ENABLE_NLS
#include <libintl.h>
#define _(String) gettext(String)
#ifdef gettext_noop
#define N_(String) gettext_noop(String)
#else
#define N_(String) (String)
#endif
#else
#define _(String) (String)
#define N_(String) (String)
#define textdomain(String) (String)
#define gettext(String) (String)
#define dgettext(Domain,String) (String)
#define dcgettext(Domain,String,Type) (String)
#define bindtextdomain(Domain,Directory) (Domain)
#endif

/* spacing between widgets, as GNOME used to have it */
#define CGHEX_PAD_SMALL 4
#define CGHEX_PAD       8

#define DEFAULT_FONT    "Monospace 10"

#define MAX_MAX_UNDO_DEPTH 100000

#define DATA_TYPE_HEX   0
#define DATA_TYPE_ASCII 1

typedef struct _PropertyUI {
    GtkWidget *pbox;
    GtkWidget *ok, *apply, *close;
    GtkRadioButton *group_type[3];
    GtkWidget *font_entry, *font_button, *undo_spin;
    GtkWidget *offset_menu, *offset_choice[3];
    GtkWidget *format, *offsets_col;
} PropertyUI;

typedef struct _JumpDialog {
    GtkWidget *window;
    GtkWidget *int_entry;
    GtkWidget *ok, *cancel;
} JumpDialog;

typedef struct _ReplaceDialog {
    GtkWidget *window;
    GtkWidget *f_string, *r_string;
    GtkWidget *replace, *replace_all, *next, *close;
    GtkWidget *type_button[2];

    gint search_type;
} ReplaceDialog;

typedef struct _FindDialog {
    GtkWidget *window;
    GtkWidget *f_string;
    GtkWidget *f_next, *f_prev, *f_close;
    GtkWidget *type_button[2];

    gint search_type;
} FindDialog;

typedef struct _Converter {
    GtkWidget *window;
    GtkWidget *entry[5];   /* binary, octal, decimal, hex, ASCII */
    GtkWidget *close;
    GtkWidget *get;

    gulong value;
} Converter;

extern FindDialog     *find_dialog;
extern ReplaceDialog  *replace_dialog;
extern JumpDialog     *jump_dialog;
extern Converter      *converter;
extern GtkWidget      *char_table;
extern PropertyUI     *prefs_ui;

/* our preferred settings; as only one copy of them is required,
   we'll make them global vars, although this is a bit ugly */
extern GdkFont    *def_font;
extern gchar      *def_font_name;
extern guint      max_undo_depth;
extern gchar      *offset_fmt;
extern gboolean   show_offsets_column;
extern gint       def_group_type;

extern guint group_type[3];
extern gchar *group_type_label[3];

extern guint search_type;
extern gchar *search_type_label[2];

/* the main window (window.c): documents, their views and the menus */
extern GtkWidget      *main_window;
extern GtkItemFactory *main_menu;
/* its icon, once the window is made: NULL if it could not be */
extern GdkPixmap      *main_icon;
extern GdkBitmap      *main_icon_mask;

void         create_main_window    (void);
HexDocument *get_active_document   (void);
GtkHex      *get_active_view       (void);
GList       *get_documents         (void);
gboolean     open_document         (const gchar *filename);
void         add_document          (HexDocument *doc);
void         add_view              (HexDocument *doc);
gboolean     remove_view           (GtkHex *view);
gboolean     remove_document       (HexDocument *doc);
gboolean     remove_all_documents  (void);
void         document_renamed      (HexDocument *doc);
void         view_changed          (void);
void         quit_app              (void);

/* the statusbar */
void set_status   (const gchar *msg);
void flash_status (const gchar *msg);
void push_hint    (const gchar *msg);
void pop_hint     (void);

/* the main menus (ui.c) */
GtkItemFactory *create_main_menu (GtkWidget *window, GtkAccelGroup *accel_group);
void            set_menu_active  (const gchar *path, gboolean active);
void            set_menu_sensitive (const gchar *path, gboolean sensitive);
extern gboolean menu_callbacks_blocked;

/* the document's menus (hex-document-ui.c) */
void hex_document_set_menu_sensitivity (HexDocument *doc);
void set_byte_cb   (gpointer data, guint action, GtkWidget *w);
void set_word_cb   (gpointer data, guint action, GtkWidget *w);
void set_long_cb   (gpointer data, guint action, GtkWidget *w);
void insert_cb     (gpointer data, guint action, GtkWidget *w);
void find_cb       (gpointer data, guint action, GtkWidget *w);
void replace_cb    (gpointer data, guint action, GtkWidget *w);
void jump_cb       (gpointer data, guint action, GtkWidget *w);
void undo_cb       (gpointer data, guint action, GtkWidget *w);
void redo_cb       (gpointer data, guint action, GtkWidget *w);
void add_view_cb   (gpointer data, guint action, GtkWidget *w);
void remove_view_cb(gpointer data, guint action, GtkWidget *w);

/* creation of dialogs */
FindDialog    *create_find_dialog    (void);
ReplaceDialog *create_replace_dialog (void);
JumpDialog    *create_jump_dialog    (void);
Converter     *create_converter      (void);
GtkWidget     *create_char_table     (void);
PropertyUI    *create_prefs_dialog   (void);

/* various ui convenience functions */
void create_dialog_title   (GtkWidget *, gchar *);
gint ask_user              (const gchar *message, const gchar **buttons,
                            gint default_button);
void show_error            (const gchar *message);
void present_window        (GtkWidget *window);

/* config stuff */
void save_configuration    (void);
void load_configuration    (void);
gboolean offset_format_valid (const gchar *fmt, gint *bad_pos);

/* hiding widgets on cancel or delete_event */
gint delete_event_cb(GtkWidget *, GdkEventAny *);
void cancel_cb(GtkWidget *, GtkWidget *);

#endif
