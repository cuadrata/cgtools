/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* ui.c - main menus and callbacks; utility functions

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

#include <string.h>

#include <gtk/gtk.h>

#include "cghex.h"

static void open_selected_file(GtkWidget *w, GtkWidget *fs);
static void save_selected_file(GtkWidget *w, GtkWidget *fs);
static void export_html_selected_file(GtkWidget *w, GtkWidget *fs);

/* callbacks for global menus */
static void quit_app_cb     (gpointer data, guint action, GtkWidget *w);
static void open_cb         (gpointer data, guint action, GtkWidget *w);
static void close_cb        (gpointer data, guint action, GtkWidget *w);
static void save_cb         (gpointer data, guint action, GtkWidget *w);
static void save_as_cb      (gpointer data, guint action, GtkWidget *w);
static void export_html_cb  (gpointer data, guint action, GtkWidget *w);
static void revert_cb       (gpointer data, guint action, GtkWidget *w);
static void prefs_cb        (gpointer data, guint action, GtkWidget *w);
static void converter_cb    (gpointer data, guint action, GtkWidget *w);
static void char_table_cb   (gpointer data, guint action, GtkWidget *w);
static void about_cb        (gpointer data, guint action, GtkWidget *w);

/*
 * The menus. An entry is its parent's path, its label and what it
 * does; the label is translated, the path an item is found by is not.
 * The hint is shown in the statusbar while the item is selected.
 */
typedef struct _MenuEntry {
    const gchar *parent;
    const gchar *label;
    const gchar *accelerator;
    void (*callback)(gpointer, guint, GtkWidget *);
    const gchar *item_type;
    const gchar *hint;
} MenuEntry;

static const MenuEntry menu_entries[] = {
    { "",       N_("_File"), NULL, NULL, "<Branch>", NULL },
    { "/File",  N_("_Open..."), "<control>O", open_cb, NULL,
      N_("Open a file") },
    { "/File",  N_("_Save"), "<control>S", save_cb, NULL,
      N_("Save the current file") },
    { "/File",  N_("Save _As..."), "<shift><control>S", save_as_cb, NULL,
      N_("Save the current file with a different name") },
    { "/File",  N_("Export to HTML..."), NULL, export_html_cb, NULL,
      N_("Export data to HTML source") },
    { "/File",  N_("_Revert"), NULL, revert_cb, NULL,
      N_("Revert to a saved version of the file") },
    { "/File",  "sep1", NULL, NULL, "<Separator>", NULL },
    { "/File",  N_("_Close"), "<control>W", close_cb, NULL,
      N_("Close the current file") },
    { "/File",  N_("E_xit"), "<control>Q", quit_app_cb, NULL,
      N_("Exit the program") },

    { "",       N_("_Edit"), NULL, NULL, "<Branch>", NULL },
    { "/Edit",  N_("_Undo"), "<control>Z", undo_cb, NULL,
      N_("Undo the last action") },
    { "/Edit",  N_("_Redo"), "<shift><control>Z", redo_cb, NULL,
      N_("Redo the undone action") },
    { "/Edit",  "sep1", NULL, NULL, "<Separator>", NULL },
    { "/Edit",  N_("_Find..."), "<control>F", find_cb, NULL,
      N_("Search for a string") },
    { "/Edit",  N_("R_eplace..."), "<control>R", replace_cb, NULL,
      N_("Search for and replace a string") },
    { "/Edit",  "sep2", NULL, NULL, "<Separator>", NULL },
    { "/Edit",  N_("_Jump to Byte..."), "<control>J", jump_cb, NULL,
      N_("Jump to a certain position") },
    { "/Edit",  "sep3", NULL, NULL, "<Separator>", NULL },
    { "/Edit",  N_("_Insert mode"), "Insert", insert_cb, "<ToggleItem>",
      N_("Insert/overwrite data") },
    { "/Edit",  "sep4", NULL, NULL, "<Separator>", NULL },
    { "/Edit",  N_("_Group Data As"), NULL, NULL, "<Branch>", NULL },
    { "/Edit/Group Data As", N_("_Bytes"), NULL, set_byte_cb, "<RadioItem>",
      N_("Group data by 8 bits") },
    { "/Edit/Group Data As", N_("_Words"), NULL, set_word_cb,
      "/Edit/Group Data As/Bytes", N_("Group data by 16 bits") },
    { "/Edit/Group Data As", N_("_Longwords"), NULL, set_long_cb,
      "/Edit/Group Data As/Bytes", N_("Group data by 32 bits") },

    { "",       N_("_View"), NULL, NULL, "<Branch>", NULL },
    { "/View",  N_("_Add view"), NULL, add_view_cb, NULL,
      N_("Add a new view of the buffer") },
    { "/View",  N_("_Remove view"), NULL, remove_view_cb, NULL,
      N_("Remove the current view of the buffer") },

    { "",       N_("Tools"), NULL, NULL, "<Branch>", NULL },
    { "/Tools", N_("Con_verter..."), NULL, converter_cb, NULL,
      N_("Open base conversion dialog") },
    { "/Tools", N_("Character Table..."), NULL, char_table_cb, NULL,
      N_("Show the character table") },

    { "",       N_("_Settings"), NULL, NULL, "<Branch>", NULL },
    { "/Settings", N_("_Preferences..."), NULL, prefs_cb, NULL,
      N_("Configure the application") },

    /* filled in with the open files by window.c */
    { "",       N_("Fi_les"), NULL, NULL, "<Branch>", NULL },

    { "",       N_("_Help"), NULL, NULL, "<Branch>", NULL },
    { "/Help",  N_("_About..."), NULL, about_cb, NULL,
      N_("About this application") },
};

guint group_type[3] = {
    GROUP_BYTE,
    GROUP_WORD,
    GROUP_LONG,
};

gchar *group_type_label[3] = {
    N_("Bytes"),
    N_("Words"),
    N_("Longwords"),
};

guint search_type = 0;
gchar *search_type_label[] = {
    N_("hex data"),
    N_("ASCII data"),
};

gboolean menu_callbacks_blocked = FALSE;

static GtkWidget *file_sel = NULL;

/* only the last part of a path is a label */
static gchar *menu_translate(const gchar *path, gpointer data G_GNUC_UNUSED)
{
    const gchar *label = strrchr(path, '/');

    return _(label ? label + 1 : path);
}

static void menu_hint_select_cb(GtkWidget *item G_GNUC_UNUSED, const gchar *hint)
{
    push_hint(_(hint));
}

static void menu_hint_deselect_cb(GtkWidget *item G_GNUC_UNUSED, const gchar *hint G_GNUC_UNUSED)
{
    pop_hint();
}

/* a path as the factory knows it: without the underscores */
static gchar *menu_path_strip(const gchar *path)
{
    gchar *stripped, *q;

    stripped = q = g_malloc(strlen(path) + 1);
    for(; *path; path++)
        if(*path != '_')
            *q++ = *path;
    *q = 0;

    return stripped;
}

GtkItemFactory *create_main_menu(GtkWidget *window G_GNUC_UNUSED, GtkAccelGroup *accel_group)
{
    GtkItemFactory *ifactory;
    GtkItemFactoryEntry entry;
    GtkWidget *item;
    gchar *path, *stripped;
    guint i;

    ifactory = gtk_item_factory_new(GTK_TYPE_MENU_BAR, "<main>", accel_group);
    gtk_item_factory_set_translate_func(ifactory, menu_translate, NULL, NULL);

    for(i = 0; i < sizeof(menu_entries) / sizeof(menu_entries[0]); i++) {
        const MenuEntry *me = &menu_entries[i];

        /* the parent is given without underscores; its entry has them */
        path = g_strconcat(me->parent, "/", me->label, NULL);

        entry.path = path;
        entry.accelerator = (gchar *)me->accelerator;
        entry.callback = (GtkItemFactoryCallback)me->callback;
        entry.callback_action = 0;
        entry.item_type = (gchar *)me->item_type;
        gtk_item_factory_create_item(ifactory, &entry, NULL, 1);

        if(me->hint) {
            stripped = menu_path_strip(path);
            item = gtk_item_factory_get_item(ifactory, stripped);
            if(item) {
                gtk_signal_connect(GTK_OBJECT(item), "select",
                                   GTK_SIGNAL_FUNC(menu_hint_select_cb),
                                   (gpointer)me->hint);
                gtk_signal_connect(GTK_OBJECT(item), "deselect",
                                   GTK_SIGNAL_FUNC(menu_hint_deselect_cb),
                                   (gpointer)me->hint);
            }
            g_free(stripped);
        }

        g_free(path);
    }

    return ifactory;
}

void set_menu_sensitive(const gchar *path, gboolean sensitive)
{
    GtkWidget *item;

    if(main_menu && (item = gtk_item_factory_get_item(main_menu, path)) != NULL)
        gtk_widget_set_sensitive(item, sensitive);
}

void set_menu_active(const gchar *path, gboolean active)
{
    GtkWidget *item;

    if(main_menu && (item = gtk_item_factory_get_item(main_menu, path)) != NULL &&
       GTK_IS_CHECK_MENU_ITEM(item))
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), active);
}

void cancel_cb(GtkWidget *w G_GNUC_UNUSED, GtkWidget *me)
{
    gtk_widget_hide(me);
}

gint delete_event_cb(GtkWidget *w, GdkEventAny *e G_GNUC_UNUSED)
{
    gtk_widget_hide(w);

    return TRUE;
}

/* shows a window that may be hidden, or raises it if it is not */
void present_window(GtkWidget *window)
{
    if(!GTK_WIDGET_VISIBLE(window)) {
        gtk_window_position(GTK_WINDOW(window), GTK_WIN_POS_MOUSE);
        gtk_widget_show(window);
    }
    gdk_window_raise(window->window);
}

/*
 * a question, answered by one of the buttons: returns the index of the
 * button, or -1 if the dialog was closed instead
 */
typedef struct _AskData {
    gint reply;
    gboolean done;
} AskData;

static void ask_button_cb(GtkWidget *button, AskData *ask)
{
    if(ask->done)
        return;
    ask->reply = GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(button),
                                                     "cghex-reply"));
    ask->done = TRUE;
    gtk_main_quit();
}

static gint ask_delete_cb(GtkWidget *w G_GNUC_UNUSED, GdkEventAny *e G_GNUC_UNUSED, AskData *ask)
{
    if(!ask->done) {
        ask->reply = -1;
        ask->done = TRUE;
        gtk_main_quit();
    }

    return TRUE;
}

gint ask_user(const gchar *message, const gchar **buttons, gint default_button)
{
    GtkWidget *dialog, *label, *button, *focus = NULL;
    AskData ask = { -1, FALSE };
    gint i;

    dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "CGHex");
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_policy(GTK_WINDOW(dialog), FALSE, FALSE, TRUE);
    if(main_window && GTK_WIDGET_VISIBLE(main_window)) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(main_window));
        gtk_window_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
    }
    gtk_signal_connect(GTK_OBJECT(dialog), "delete_event",
                       GTK_SIGNAL_FUNC(ask_delete_cb), &ask);

    label = gtk_label_new(message);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
    gtk_misc_set_padding(GTK_MISC(label), 2*CGHEX_PAD, 2*CGHEX_PAD);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), label, TRUE, TRUE, 0);

    gtk_container_border_width(GTK_CONTAINER(GTK_DIALOG(dialog)->action_area),
                               CGHEX_PAD_SMALL);
    for(i = 0; buttons[i]; i++) {
        button = gtk_button_new_with_label(_(buttons[i]));
        GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
        gtk_object_set_data(GTK_OBJECT(button), "cghex-reply", GINT_TO_POINTER(i));
        gtk_signal_connect(GTK_OBJECT(button), "clicked",
                           GTK_SIGNAL_FUNC(ask_button_cb), &ask);
        gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area), button,
                           TRUE, TRUE, 0);
        if(i == default_button)
            focus = button;
    }

    gtk_widget_show_all(dialog);
    if(focus) {
        gtk_widget_grab_default(focus);
        gtk_widget_grab_focus(focus);
    }

    gtk_main();

    gtk_widget_destroy(dialog);

    return ask.reply;
}

void show_error(const gchar *message)
{
    static const gchar *buttons[] = { N_("OK"), NULL };

    ask_user(message, buttons, 0);
}

void create_dialog_title(GtkWidget *window, gchar *title)
{
    HexDocument *doc;
    gchar *full_title;

    if(!window)
        return;

    doc = get_active_document();
    full_title = g_strdup_printf(title, doc ? doc->path_end : "");

    gtk_window_set_title(GTK_WINDOW(window), full_title);
    g_free(full_title);
}

/*
 * the file selection: one at a time, modal, as the document it acts
 * on is the active one when it is done
 */
static void file_sel_destroy_cb(GtkWidget *w G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
    file_sel = NULL;
}

static void file_sel_cancel_cb(GtkWidget *w G_GNUC_UNUSED, GtkWidget *fs)
{
    gtk_widget_destroy(fs);
}

static GtkWidget *create_file_sel(const gchar *title, const gchar *filename,
                                  void (*ok_cb)(GtkWidget *, GtkWidget *))
{
    GtkWidget *fs;

    fs = gtk_file_selection_new(title);
    gtk_window_set_modal(GTK_WINDOW(fs), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(fs), GTK_WINDOW(main_window));
    gtk_window_position(GTK_WINDOW(fs), GTK_WIN_POS_MOUSE);

    if(filename)
        gtk_file_selection_set_filename(GTK_FILE_SELECTION(fs), filename);

    gtk_signal_connect(GTK_OBJECT(GTK_FILE_SELECTION(fs)->ok_button),
                       "clicked", GTK_SIGNAL_FUNC(ok_cb), fs);
    gtk_signal_connect(GTK_OBJECT(GTK_FILE_SELECTION(fs)->cancel_button),
                       "clicked", GTK_SIGNAL_FUNC(file_sel_cancel_cb), fs);
    gtk_signal_connect(GTK_OBJECT(fs), "destroy",
                       GTK_SIGNAL_FUNC(file_sel_destroy_cb), NULL);

    gtk_widget_show(fs);

    return fs;
}

/*
 * callbacks for global menus
 */
static void about_destroy_cb(GtkObject *obj G_GNUC_UNUSED, GtkWidget **about)
{
    *about = NULL;
}

static void about_cb(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *widget G_GNUC_UNUSED)
{
    static GtkWidget *about = NULL;
    static const gchar *authors[] = {
        "Jaka Mocnik <jaka.mocnik@kiss.uni-lj.si>",
        "Chema Celorio <chema@celorio.com>",
        NULL
    };
    GtkWidget *vbox, *label, *button;
    gchar *text;
    gint i;

    if(about) {
        gdk_window_raise(about->window);
        return;
    }

    about = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(about), _("About CGHex"));
    gtk_window_set_policy(GTK_WINDOW(about), FALSE, FALSE, TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(about), GTK_WINDOW(main_window));
    gtk_window_position(GTK_WINDOW(about), GTK_WIN_POS_CENTER);
    gtk_signal_connect(GTK_OBJECT(about), "destroy",
                       GTK_SIGNAL_FUNC(about_destroy_cb), &about);

    vbox = gtk_vbox_new(FALSE, CGHEX_PAD_SMALL);
    gtk_container_border_width(GTK_CONTAINER(vbox), 2*CGHEX_PAD);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(about)->vbox), vbox, TRUE, TRUE, 0);

    if(main_icon)
        gtk_box_pack_start(GTK_BOX(vbox),
                           gtk_pixmap_new(main_icon, main_icon_mask),
                           FALSE, FALSE, 0);

    text = g_strdup_printf("CGHex %s", PACKAGE_VERSION);
    label = gtk_label_new(text);
    g_free(text);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

    label = gtk_label_new(_("CGHex, a binary file editor"));
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

    label = gtk_label_new("(C) 1998, 1999, 2000 Jaka Mocnik");
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, CGHEX_PAD_SMALL);

    for(i = 0; authors[i]; i++) {
        label = gtk_label_new(authors[i]);
        gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    }

    label = gtk_label_new(_("Released under the terms of the GNU General Public License"));
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, CGHEX_PAD_SMALL);

    button = gtk_button_new_with_label(_("OK"));
    GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
    gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
                              GTK_SIGNAL_FUNC(gtk_widget_destroy),
                              GTK_OBJECT(about));
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(about)->action_area), button,
                       TRUE, TRUE, 0);

    gtk_widget_show_all(about);
    gtk_widget_grab_default(button);
}

static void quit_app_cb(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *widget G_GNUC_UNUSED)
{
    quit_app();
}

static void save_cb(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *w G_GNUC_UNUSED)
{
    HexDocument *doc;
    gchar *flash;

    if((doc = get_active_document()) == NULL)
        return;

    if(!hex_document_write(doc))
        show_error(_("Error saving file!"));
    else {
        flash = g_strdup_printf(_("Saved buffer to file %s"), doc->file_name);
        flash_status(flash);
        g_free(flash);
    }
}

static void open_cb(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *w G_GNUC_UNUSED)
{
    HexDocument *doc;

    if(file_sel)
        return;

    doc = get_active_document();
    file_sel = create_file_sel(_("Select a file to open"),
                               doc ? doc->file_name : NULL,
                               open_selected_file);
}

static void save_as_cb(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *w G_GNUC_UNUSED)
{
    HexDocument *doc;

    if(file_sel || (doc = get_active_document()) == NULL)
        return;

    file_sel = create_file_sel(_("Select a file to save buffer as"),
                               doc->file_name, save_selected_file);
}

static void export_html_cb(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *w G_GNUC_UNUSED)
{
    HexDocument *doc;

    if(file_sel || (doc = get_active_document()) == NULL)
        return;

    file_sel = create_file_sel(_("Select path and file name for the HTML source"),
                               doc->file_name, export_html_selected_file);
}

static void export_html_selected_file(GtkWidget *w G_GNUC_UNUSED, GtkWidget *fs)
{
    gchar *html_path, *sep, *base_name;
    GtkHex *view;
    HexDocument *doc;

    html_path = g_strdup(gtk_file_selection_get_filename(GTK_FILE_SELECTION(fs)));
    gtk_widget_destroy(fs);

    if((view = get_active_view()) == NULL) {
        g_free(html_path);
        return;
    }
    doc = view->document;

    sep = html_path + strlen(html_path) - 1;
    while(sep >= html_path && *sep != '/')
        sep--;
    if(sep >= html_path)
        *sep = 0;
    base_name = sep + 1;
    sep = strstr(base_name, ".htm");
    if(sep)
        *sep = 0;

    if(*base_name != 0 &&
       !hex_document_export_html(doc, html_path, base_name, 0, doc->file_size,
                                 view->cpl, view->vis_lines, view->group_type))
        show_error(_("Can't open file for writing!"));

    g_free(html_path);
}

static void close_cb(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *w G_GNUC_UNUSED)
{
    HexDocument *doc;

    if((doc = get_active_document()) != NULL)
        remove_document(doc);
}

static void converter_cb(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *w G_GNUC_UNUSED)
{
    if(!converter)
        converter = create_converter();

    present_window(converter->window);
}

static void char_table_cb(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *w G_GNUC_UNUSED)
{
    if(!char_table)
        char_table = create_char_table();

    present_window(char_table);
}

static void prefs_cb(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *w G_GNUC_UNUSED)
{
    if(!prefs_ui)
        prefs_ui = create_prefs_dialog();

    present_window(prefs_ui->pbox);
}

static void revert_cb(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *w G_GNUC_UNUSED)
{
    static const gchar *buttons[] = { N_("Yes"), N_("No"), NULL };
    HexDocument *doc;
    gchar *msg, *flash;
    gint reply;

    if((doc = get_active_document()) == NULL || !doc->changed)
        return;

    msg = g_strdup_printf(_("Really revert file %s?"), doc->path_end);
    reply = ask_user(msg, buttons, 1);
    g_free(msg);

    if(reply == 0) {
        hex_document_read(doc);
        flash = g_strdup_printf(_("Reverted buffer from file %s"), doc->file_name);
        flash_status(flash);
        g_free(flash);
    }
}

static void open_selected_file(GtkWidget *w G_GNUC_UNUSED, GtkWidget *fs)
{
    gchar *filename;

    filename = g_strdup(gtk_file_selection_get_filename(GTK_FILE_SELECTION(fs)));
    gtk_widget_destroy(fs);

    open_document(filename);
    g_free(filename);
}

static void save_selected_file(GtkWidget *w G_GNUC_UNUSED, GtkWidget *fs)
{
    HexDocument *doc;
    FILE *file;
    gchar *filename, *flash;

    filename = g_strdup(gtk_file_selection_get_filename(GTK_FILE_SELECTION(fs)));
    gtk_widget_destroy(fs);

    if((doc = get_active_document()) == NULL) {
        g_free(filename);
        return;
    }

    if((file = fopen(filename, "w")) != NULL) {
        if(hex_document_write_to_file(doc, file)) {
            hex_document_set_file_name(doc, filename);
            doc->changed = FALSE;
            document_renamed(doc);

            flash = g_strdup_printf(_("Saved buffer to file %s"), doc->file_name);
            flash_status(flash);
            g_free(flash);
        }
        else
            show_error(_("Error saving file!"));
        fclose(file);
    }
    else
        show_error(_("Can't open file for writing!"));

    g_free(filename);
}
