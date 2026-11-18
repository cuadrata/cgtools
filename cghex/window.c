/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* window.c - the main window: the documents, their views and the statusbar

   Copyright (C) 2026 The CGTK authors

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
*/

/*
 * What GnomeMDI used to do for CGHex, in its notebook mode: one window,
 * a page for each view, and a document stays open for as long as it
 * has one. The menus are the same for every document; what does not
 * apply without one is made insensitive.
 */

#include <config.h>

#include <string.h>

#include <gtk/gtk.h>

#include "cghex.h"

#include "icons/cghex.xpm"

#define FLASH_TIMEOUT 3000

enum {
    TARGET_URI_LIST
};

GtkWidget *main_window = NULL;
GtkItemFactory *main_menu = NULL;
GdkPixmap *main_icon = NULL;
GdkBitmap *main_icon_mask = NULL;

static GtkWidget *notebook = NULL;
static GtkWidget *statusbar = NULL;
static GList *documents = NULL;

static guint status_context, flash_context, hint_context;
static guint flash_timeout = 0;

static void cursor_moved_cb(GtkHex *gh, gpointer data);

/*
 * the statusbar: the cursor offset stays, flashed messages go away
 * again after a while
 */
void set_status(const gchar *msg)
{
    if(!statusbar)
        return;

    gtk_statusbar_pop(GTK_STATUSBAR(statusbar), status_context);
    if(msg)
        gtk_statusbar_push(GTK_STATUSBAR(statusbar), status_context, msg);
}

static gint flash_timeout_cb(gpointer data G_GNUC_UNUSED)
{
    gtk_statusbar_pop(GTK_STATUSBAR(statusbar), flash_context);
    flash_timeout = 0;

    return FALSE;
}

void flash_status(const gchar *msg)
{
    if(!statusbar)
        return;

    if(flash_timeout) {
        gtk_timeout_remove(flash_timeout);
        gtk_statusbar_pop(GTK_STATUSBAR(statusbar), flash_context);
    }

    gtk_statusbar_push(GTK_STATUSBAR(statusbar), flash_context, msg);
    flash_timeout = gtk_timeout_add(FLASH_TIMEOUT, flash_timeout_cb, NULL);
}

/* the hint for the menu item under the pointer */
void push_hint(const gchar *msg)
{
    if(statusbar)
        gtk_statusbar_push(GTK_STATUSBAR(statusbar), hint_context, msg);
}

void pop_hint(void)
{
    if(statusbar)
        gtk_statusbar_pop(GTK_STATUSBAR(statusbar), hint_context);
}

/*
 * the documents and their views
 */
GList *get_documents(void)
{
    return documents;
}

GtkHex *get_active_view(void)
{
    GtkWidget *page;
    gint n;

    if(!notebook)
        return NULL;

    n = gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook));
    if(n < 0)
        return NULL;

    page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), n);

    return page ? GTK_HEX(page) : NULL;
}

HexDocument *get_active_document(void)
{
    GtkHex *view = get_active_view();

    return view ? view->document : NULL;
}

static void update_tabs(void)
{
    /* tabs only once there is something to choose between */
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(notebook),
                               g_list_length(GTK_NOTEBOOK(notebook)->children) > 1);
}

static void files_menu_activate_cb(GtkWidget *item, HexDocument *doc)
{
    gint n;

    if(menu_callbacks_blocked || !GTK_CHECK_MENU_ITEM(item)->active)
        return;

    if(doc == get_active_document() || !doc->views)
        return;

    n = gtk_notebook_page_num(GTK_NOTEBOOK(notebook), GTK_WIDGET(doc->views->data));
    gtk_notebook_set_page(GTK_NOTEBOOK(notebook), n);
}

static void destroy_child_cb(GtkWidget *child, gpointer data G_GNUC_UNUSED)
{
    gtk_widget_destroy(child);
}

/* the Files menu lists the open documents, the active one checked */
static void update_files_menu(void)
{
    GtkWidget *menu, *item;
    HexDocument *active, *doc;
    GSList *group = NULL;
    GList *node;

    menu = gtk_item_factory_get_widget(main_menu, "/Files");
    if(!menu)
        return;

    gtk_container_foreach(GTK_CONTAINER(menu), destroy_child_cb, NULL);

    active = get_active_document();

    menu_callbacks_blocked = TRUE;
    for(node = documents; node; node = node->next) {
        doc = HEX_DOCUMENT(node->data);
        item = gtk_radio_menu_item_new_with_label(group, doc->path_end);
        group = gtk_radio_menu_item_group(GTK_RADIO_MENU_ITEM(item));
        gtk_check_menu_item_set_show_toggle(GTK_CHECK_MENU_ITEM(item), TRUE);
        if(doc == active)
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), TRUE);
        gtk_signal_connect(GTK_OBJECT(item), "activate",
                           GTK_SIGNAL_FUNC(files_menu_activate_cb), doc);
        gtk_menu_append(GTK_MENU(menu), item);
        gtk_widget_show(item);
    }
    menu_callbacks_blocked = FALSE;

    set_menu_sensitive("/Files", documents != NULL);
}

static void update_title(void)
{
    HexDocument *doc = get_active_document();
    gchar *title;

    if(doc) {
        title = g_strdup_printf("CGHex - %s", doc->path_end);
        gtk_window_set_title(GTK_WINDOW(main_window), title);
        g_free(title);
    }
    else
        gtk_window_set_title(GTK_WINDOW(main_window), "CGHex");
}

/*
 * brings everything that depends on the active view up to date with it
 */
void view_changed(void)
{
    GtkHex *view;
    HexDocument *doc;
    gboolean sens;
    gint i;

    if(!main_window)
        return;

    view = get_active_view();
    doc = view ? view->document : NULL;
    sens = (doc != NULL);

    set_menu_sensitive("/File/Save", sens);
    set_menu_sensitive("/File/Save As...", sens);
    set_menu_sensitive("/File/Export to HTML...", sens);
    set_menu_sensitive("/File/Revert", sens);
    set_menu_sensitive("/File/Close", sens);
    set_menu_sensitive("/Edit", sens);
    set_menu_sensitive("/View", sens);

    if(view) {
        menu_callbacks_blocked = TRUE;
        for(i = 0; i < 3; i++)
            if(view->group_type == group_type[i]) {
                gchar *path = g_strconcat("/Edit/Group Data As/",
                                          group_type_label[i], NULL);
                set_menu_active(path, TRUE);
                g_free(path);
            }
        set_menu_active("/Edit/Insert mode", view->insert);
        menu_callbacks_blocked = FALSE;

        hex_document_set_menu_sensitivity(doc);
        cursor_moved_cb(view, NULL);
    }
    else
        set_status(NULL);

    update_title();
    update_files_menu();

    if(find_dialog)
        create_dialog_title(find_dialog->window, _("CGHex (%s): Find Data"));
    if(replace_dialog)
        create_dialog_title(replace_dialog->window, _("CGHex (%s): Find & Replace Data"));
    if(jump_dialog)
        create_dialog_title(jump_dialog->window, _("CGHex (%s): Jump To Byte"));
}

static void switch_page_cb(GtkNotebook *nb G_GNUC_UNUSED, GtkNotebookPage *page G_GNUC_UNUSED,
                           guint page_num G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
    GtkHex *view = get_active_view();

    view_changed();

    if(view)
        gtk_widget_grab_focus(GTK_WIDGET(view));
}

static void cursor_moved_cb(GtkHex *gh, gpointer data G_GNUC_UNUSED)
{
    gchar *format, *cursor_pos;

    if(gh != get_active_view())
        return;

    format = g_strdup_printf(_("Offset: %s"), offset_fmt);
    cursor_pos = g_strdup_printf(format, gtk_hex_get_cursor(gh));
    set_status(cursor_pos);
    g_free(cursor_pos);
    g_free(format);
}

void add_view(HexDocument *doc)
{
    GtkWidget *view, *label;
    gint n;

    view = hex_document_add_view(doc);
    gtk_signal_connect(GTK_OBJECT(view), "cursor_moved",
                       GTK_SIGNAL_FUNC(cursor_moved_cb), NULL);

    label = gtk_label_new(doc->path_end);
    gtk_object_set_data(GTK_OBJECT(view), "cghex-tab-label", label);

    gtk_widget_show(view);
    gtk_widget_set_usize(notebook, -1, -1);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), view, label);
    update_tabs();

    n = gtk_notebook_page_num(GTK_NOTEBOOK(notebook), view);
    if(n == gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook))) {
        /* the first page: the notebook made it current without saying */
        view_changed();
        gtk_widget_grab_focus(view);
    }
    else
        gtk_notebook_set_page(GTK_NOTEBOOK(notebook), n);
}

void add_document(HexDocument *doc)
{
    gtk_object_ref(GTK_OBJECT(doc));
    gtk_object_sink(GTK_OBJECT(doc));

    documents = g_list_append(documents, doc);

    add_view(doc);
}

gboolean open_document(const gchar *filename)
{
    HexDocument *doc;
    gchar *flash;

    if((doc = hex_document_new(filename)) == NULL) {
        show_error(_("Can not open file!"));
        return FALSE;
    }

    add_document(doc);

    flash = g_strdup_printf(_("Loaded file %s"), doc->file_name);
    flash_status(flash);
    g_free(flash);

    return TRUE;
}

static void remove_page(GtkWidget *view)
{
    gint n;

    n = gtk_notebook_page_num(GTK_NOTEBOOK(notebook), view);
    if(n >= 0)
        gtk_notebook_remove_page(GTK_NOTEBOOK(notebook), n);
}

gboolean remove_document(HexDocument *doc)
{
    static const gchar *buttons[] = { N_("Yes"), N_("No"), N_("Cancel"), NULL };
    gchar *msg;
    gint reply;

    if(hex_document_has_changed(doc)) {
        msg = g_strdup_printf(_("File %s has changed since last save.\n"
                                "Do you want to save changes?"), doc->path_end);
        reply = ask_user(msg, buttons, 2);
        g_free(msg);

        if(reply == 0) {
            if(!hex_document_write(doc)) {
                show_error(_("Error saving file!"));
                return FALSE;
            }
        }
        else if(reply != 1)
            return FALSE;
    }

    while(doc->views)
        remove_page(GTK_WIDGET(doc->views->data));

    documents = g_list_remove(documents, doc);
    gtk_object_destroy(GTK_OBJECT(doc));
    gtk_object_unref(GTK_OBJECT(doc));

    update_tabs();
    view_changed();

    return TRUE;
}

gboolean remove_view(GtkHex *view)
{
    HexDocument *doc = view->document;

    /* a document goes when its last view does */
    if(g_list_length(doc->views) == 1)
        return remove_document(doc);

    remove_page(GTK_WIDGET(view));
    update_tabs();
    view_changed();

    return TRUE;
}

gboolean remove_all_documents(void)
{
    while(documents) {
        if(!remove_document(HEX_DOCUMENT(documents->data)))
            return FALSE;
    }

    return TRUE;
}

void document_renamed(HexDocument *doc)
{
    GtkWidget *label;
    GList *node;

    for(node = doc->views; node; node = node->next) {
        label = gtk_object_get_data(GTK_OBJECT(node->data), "cghex-tab-label");
        if(label)
            gtk_label_set_text(GTK_LABEL(label), doc->path_end);
    }
    gtk_widget_queue_draw(notebook);

    view_changed();
}

void quit_app(void)
{
    if(remove_all_documents()) {
        save_configuration();
        gtk_main_quit();
    }
}

static gint main_window_delete_cb(GtkWidget *w G_GNUC_UNUSED, GdkEventAny *event G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
    quit_app();

    return TRUE;
}

/*
 * dropping files on the window opens them
 */
static gint hex_value(gchar c)
{
    if(c >= '0' && c <= '9')
        return c - '0';
    if(c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if(c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* a file: URI as a file name, or NULL for anything else */
static gchar *uri_to_filename(const gchar *uri)
{
    gchar *name, *q;
    const gchar *p;
    gint hi, lo;

    if(strncmp(uri, "file:", 5) != 0)
        return NULL;
    p = uri + 5;

    /* file://host/path: the host is this one, or it could not be opened */
    if(p[0] == '/' && p[1] == '/') {
        p = strchr(p + 2, '/');
        if(!p)
            return NULL;
    }

    name = q = g_malloc(strlen(p) + 1);
    while(*p) {
        if(p[0] == '%' && (hi = hex_value(p[1])) >= 0 &&
           (lo = hex_value(p[2])) >= 0) {
            *q++ = (gchar)(hi << 4 | lo);
            p += 3;
        }
        else
            *q++ = *p++;
    }
    *q = 0;

    return name;
}

static void drop_cb(GtkWidget *widget G_GNUC_UNUSED, GdkDragContext *context G_GNUC_UNUSED,
                    gint x G_GNUC_UNUSED, gint y G_GNUC_UNUSED, GtkSelectionData *selection_data,
                    guint info, guint time G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
    gchar *list, *line, *next, *name;

    if(info != TARGET_URI_LIST || selection_data->length <= 0)
        return;

    list = g_malloc(selection_data->length + 1);
    memcpy(list, selection_data->data, selection_data->length);
    list[selection_data->length] = 0;

    for(line = list; line && *line; line = next) {
        next = strchr(line, '\n');
        if(next)
            *next++ = 0;
        if(*line && line[strlen(line) - 1] == '\r')
            line[strlen(line) - 1] = 0;

        if(*line == 0 || *line == '#')
            continue;

        if((name = uri_to_filename(line)) != NULL) {
            open_document(name);
            g_free(name);
        }
    }

    g_free(list);
}

void create_main_window(void)
{
    static GtkTargetEntry drop_types[] = {
        { "text/uri-list", 0, TARGET_URI_LIST }
    };
    GtkWidget *vbox, *menubar, *empty_view;
    GtkAccelGroup *accel_group;
    GtkRequisition req;

    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_window), "CGHex");
    gtk_window_set_wmclass(GTK_WINDOW(main_window), "cghex", "Cghex");
    gtk_window_set_policy(GTK_WINDOW(main_window), TRUE, TRUE, FALSE);
    gtk_signal_connect(GTK_OBJECT(main_window), "delete_event",
                       GTK_SIGNAL_FUNC(main_window_delete_cb), NULL);

    gtk_drag_dest_set(main_window,
                      GTK_DEST_DEFAULT_MOTION |
                      GTK_DEST_DEFAULT_HIGHLIGHT |
                      GTK_DEST_DEFAULT_DROP,
                      drop_types, sizeof(drop_types) / sizeof(drop_types[0]),
                      GDK_ACTION_COPY);
    gtk_signal_connect(GTK_OBJECT(main_window), "drag_data_received",
                       GTK_SIGNAL_FUNC(drop_cb), NULL);

    vbox = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(main_window), vbox);

    accel_group = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(main_window), accel_group);

    statusbar = gtk_statusbar_new();
    status_context = gtk_statusbar_get_context_id(GTK_STATUSBAR(statusbar), "status");
    flash_context = gtk_statusbar_get_context_id(GTK_STATUSBAR(statusbar), "flash");
    hint_context = gtk_statusbar_get_context_id(GTK_STATUSBAR(statusbar), "hint");

    main_menu = create_main_menu(main_window, accel_group);
    menubar = gtk_item_factory_get_widget(main_menu, "<main>");
    gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);

    notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(notebook), FALSE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(notebook), FALSE);
    gtk_signal_connect_after(GTK_OBJECT(notebook), "switch_page",
                             GTK_SIGNAL_FUNC(switch_page_cb), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

    gtk_box_pack_end(GTK_BOX(vbox), statusbar, FALSE, FALSE, 0);

    /* with nothing open, the window is as big as a view would be */
    empty_view = gtk_hex_new(NULL);
    gtk_object_ref(GTK_OBJECT(empty_view));
    gtk_object_sink(GTK_OBJECT(empty_view));
    if(def_font)
        gtk_hex_set_font(GTK_HEX(empty_view), def_font);
    gtk_hex_set_group_type(GTK_HEX(empty_view), def_group_type);
    gtk_hex_show_offsets(GTK_HEX(empty_view), show_offsets_column);
    gtk_widget_size_request(empty_view, &req);
    gtk_widget_unref(empty_view);
    gtk_widget_set_usize(notebook, req.width, req.height);

    /* the icon: for the window, and kept for the About dialog */
    gtk_widget_realize(main_window);
    main_icon = gdk_pixmap_create_from_xpm_d(main_window->window,
                                             &main_icon_mask, NULL,
                                             cghex_xpm);
    if(main_icon)
        gdk_window_set_icon(main_window->window, NULL,
                            main_icon, main_icon_mask);

    gtk_widget_show_all(main_window);

    view_changed();
}
