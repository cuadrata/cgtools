/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* preferences.c - setting the preferences

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

static void set_prefs(PropertyUI *pui);
static void browse_font_cb(GtkWidget *w, PropertyUI *pui);
static void max_undo_changed_cb(GtkAdjustment *adj, PropertyUI *pui);
static void properties_modified_cb(GtkWidget *w, PropertyUI *pui);
static void offset_cb(GtkWidget *w, PropertyUI *pui);
static void ok_cb(GtkWidget *w, PropertyUI *pui);
static gboolean apply_changes(PropertyUI *pui);

PropertyUI *prefs_ui = NULL;

GdkFont *def_font = NULL;
gchar *def_font_name = NULL;

/* like a GnomePropertyBox: Apply is there to press once something changed */
static void set_modified(PropertyUI *pui, gboolean modified)
{
    gtk_widget_set_sensitive(pui->apply, modified);
}

static void prefs_close_cb(GtkWidget *w G_GNUC_UNUSED, PropertyUI *pui)
{
    gtk_widget_hide(pui->pbox);
}

static gint prefs_delete_cb(GtkWidget *w G_GNUC_UNUSED, GdkEventAny *e G_GNUC_UNUSED, PropertyUI *pui)
{
    gtk_widget_hide(pui->pbox);

    return TRUE;
}

/* a dialog that is shown again starts from the settings as they are */
static void prefs_show_cb(GtkWidget *w G_GNUC_UNUSED, PropertyUI *pui)
{
    set_prefs(pui);
}

static void apply_cb(GtkWidget *w G_GNUC_UNUSED, PropertyUI *pui)
{
    if(apply_changes(pui))
        set_modified(pui, FALSE);
}

static void ok_cb(GtkWidget *w G_GNUC_UNUSED, PropertyUI *pui)
{
    if(apply_changes(pui)) {
        set_modified(pui, FALSE);
        gtk_widget_hide(pui->pbox);
    }
}

static GtkWidget *add_button(PropertyUI *pui, const gchar *label,
                             GtkSignalFunc func)
{
    GtkWidget *button;

    button = gtk_button_new_with_label(label);
    GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
    gtk_signal_connect(GTK_OBJECT(button), "clicked", func, pui);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(pui->pbox)->action_area), button,
                       TRUE, TRUE, 0);
    gtk_widget_show(button);

    return button;
}

PropertyUI *create_prefs_dialog(void) {
    GtkWidget *vbox, *label, *frame, *box, *fbox, *notebook;
    GtkWidget *menu, *item;
    GtkAdjustment *undo_adj;
    GSList *group;
    PropertyUI *pui;

    int i;

    pui = g_new0(PropertyUI, 1);

    pui->pbox = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(pui->pbox), _("CGHex Preferences"));
    gtk_window_set_policy(GTK_WINDOW(pui->pbox), FALSE, TRUE, TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(pui->pbox), GTK_WINDOW(main_window));
    gtk_signal_connect(GTK_OBJECT(pui->pbox), "delete_event",
                       GTK_SIGNAL_FUNC(prefs_delete_cb), pui);
    gtk_signal_connect(GTK_OBJECT(pui->pbox), "show",
                       GTK_SIGNAL_FUNC(prefs_show_cb), pui);

    notebook = gtk_notebook_new();
    gtk_container_border_width(GTK_CONTAINER(notebook), CGHEX_PAD_SMALL);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(pui->pbox)->vbox), notebook,
                       TRUE, TRUE, 0);
    gtk_widget_show(notebook);

    gtk_container_border_width(GTK_CONTAINER(GTK_DIALOG(pui->pbox)->action_area),
                               CGHEX_PAD_SMALL);
    pui->ok = add_button(pui, _("OK"), GTK_SIGNAL_FUNC(ok_cb));
    pui->apply = add_button(pui, _("Apply"), GTK_SIGNAL_FUNC(apply_cb));
    pui->close = add_button(pui, _("Close"), GTK_SIGNAL_FUNC(prefs_close_cb));
    gtk_widget_grab_default(pui->ok);

    /* editing page */
    vbox = gtk_vbox_new(FALSE, 0);
    gtk_container_border_width(GTK_CONTAINER(vbox), CGHEX_PAD_SMALL);
    gtk_widget_show(vbox);

    /* max undo levels */
    undo_adj = GTK_ADJUSTMENT(gtk_adjustment_new(MIN(max_undo_depth, MAX_MAX_UNDO_DEPTH),
                                                 0, MAX_MAX_UNDO_DEPTH, 1, 10, 0));
    gtk_signal_connect(GTK_OBJECT(undo_adj), "value_changed",
                       GTK_SIGNAL_FUNC(max_undo_changed_cb), pui);

    box = gtk_hbox_new(FALSE, 0);
    gtk_widget_show(box);

    label = gtk_label_new(_("Maximum number of undo levels"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start (GTK_BOX(box), label, TRUE, TRUE, CGHEX_PAD_SMALL);
    gtk_widget_show(label);

    pui->undo_spin = gtk_spin_button_new(undo_adj, 1, 0);
    gtk_box_pack_end (GTK_BOX(box), GTK_WIDGET(pui->undo_spin), FALSE, TRUE, CGHEX_PAD);
    gtk_widget_show(pui->undo_spin);

    gtk_box_pack_start(GTK_BOX(vbox), box, FALSE, TRUE, CGHEX_PAD_SMALL);

    /* cursor offset format */
    box = gtk_hbox_new(FALSE, 0);
    gtk_widget_show(box);

    label = gtk_label_new(_("Show cursor offset in statusbar as"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start (GTK_BOX(box), label, TRUE, TRUE, CGHEX_PAD);
    gtk_widget_show(label);

    pui->format = gtk_entry_new();
    gtk_box_pack_start (GTK_BOX(box), pui->format, TRUE, TRUE, CGHEX_PAD);
    gtk_widget_show(pui->format);

    pui->offset_menu = gtk_option_menu_new();
    gtk_widget_show(pui->offset_menu);
    menu = gtk_menu_new();
    pui->offset_choice[0] = item = gtk_menu_item_new_with_label(_("Decimal"));
    gtk_signal_connect(GTK_OBJECT(item), "activate",
                       GTK_SIGNAL_FUNC(offset_cb), pui);
    gtk_widget_show(item);
    gtk_menu_append(GTK_MENU(menu), item);
    pui->offset_choice[1] = item = gtk_menu_item_new_with_label(_("Hexadecimal"));
    gtk_signal_connect(GTK_OBJECT(item), "activate",
                       GTK_SIGNAL_FUNC(offset_cb), pui);
    gtk_widget_show(item);
    gtk_menu_append(GTK_MENU(menu), item);
    pui->offset_choice[2] = item = gtk_menu_item_new_with_label(_("Custom"));
    gtk_signal_connect(GTK_OBJECT(item), "activate",
                       GTK_SIGNAL_FUNC(offset_cb), pui);
    gtk_widget_show(item);
    gtk_menu_append(GTK_MENU(menu), item);
    gtk_option_menu_set_menu(GTK_OPTION_MENU(pui->offset_menu), menu);
    gtk_box_pack_end(GTK_BOX(box), GTK_WIDGET(pui->offset_menu), FALSE, TRUE, CGHEX_PAD);

    gtk_box_pack_start(GTK_BOX(vbox), box, FALSE, TRUE, CGHEX_PAD_SMALL);

    /* show offsets check button */
    pui->offsets_col = gtk_check_button_new_with_label(_("Show offsets column"));
    gtk_box_pack_start(GTK_BOX(vbox), pui->offsets_col, FALSE, TRUE, CGHEX_PAD_SMALL);
    gtk_widget_show(pui->offsets_col);

    label = gtk_label_new(_("Editing"));
    gtk_widget_show(label);
    gtk_notebook_append_page (GTK_NOTEBOOK(notebook), vbox, label);

    /* display page */
    vbox = gtk_vbox_new(FALSE, 0);
    gtk_container_border_width(GTK_CONTAINER(vbox), CGHEX_PAD_SMALL);
    gtk_widget_show(vbox);

    /* display font */
    frame = gtk_frame_new(_("Font"));
    gtk_container_border_width(GTK_CONTAINER(frame), CGHEX_PAD_SMALL);
    gtk_widget_show(frame);

    fbox = gtk_hbox_new(0, 0);
    pui->font_entry = gtk_entry_new();
    pui->font_button = gtk_button_new_with_label(_("Browse..."));
    gtk_signal_connect(GTK_OBJECT(pui->font_button), "clicked",
                       GTK_SIGNAL_FUNC(browse_font_cb), pui);
    gtk_widget_show(pui->font_button);
    gtk_widget_show(pui->font_entry);

    gtk_container_border_width(GTK_CONTAINER(fbox), CGHEX_PAD_SMALL);
    gtk_box_pack_start (GTK_BOX (fbox), pui->font_entry, 1, 1, CGHEX_PAD);
    gtk_box_pack_end (GTK_BOX (fbox), pui->font_button, 0, 1, CGHEX_PAD);

    gtk_widget_show(fbox);
    gtk_container_add(GTK_CONTAINER(frame), fbox);

    gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, TRUE, CGHEX_PAD_SMALL);

    /* default group type */
    frame = gtk_frame_new(_("Default Group Type"));
    gtk_container_border_width(GTK_CONTAINER(frame), CGHEX_PAD_SMALL);
    gtk_widget_show(frame);

    box = gtk_vbox_new(FALSE, 0);
    gtk_container_border_width(GTK_CONTAINER(box), CGHEX_PAD_SMALL);
    gtk_widget_show(box);
    group = NULL;
    for(i = 0; i < 3; i++) {
        pui->group_type[i] = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label(group, _(group_type_label[i])));
        gtk_widget_show(GTK_WIDGET(pui->group_type[i]));
        gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(pui->group_type[i]), TRUE, TRUE, 2);
        group = gtk_radio_button_group (pui->group_type[i]);
    }
    gtk_container_add(GTK_CONTAINER(frame), box);
    gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, TRUE, CGHEX_PAD_SMALL);

    label = gtk_label_new(_("Display"));
    gtk_widget_show(label);
    gtk_notebook_append_page (GTK_NOTEBOOK(notebook), vbox, label);

    set_prefs(pui);

    /* signals have to be connected after set_prefs(), otherwise
       the dialog would start out modified */
    for(i = 0; i < 3; i++)
        gtk_signal_connect(GTK_OBJECT(pui->group_type[i]), "clicked",
                           GTK_SIGNAL_FUNC(properties_modified_cb), pui);

    gtk_signal_connect(GTK_OBJECT(pui->offsets_col), "toggled",
                       GTK_SIGNAL_FUNC(properties_modified_cb), pui);
    gtk_signal_connect(GTK_OBJECT(pui->format), "changed",
                       GTK_SIGNAL_FUNC(properties_modified_cb), pui);
    gtk_signal_connect(GTK_OBJECT(pui->font_entry), "changed",
                       GTK_SIGNAL_FUNC(properties_modified_cb), pui);

    return pui;
}

static void set_prefs(PropertyUI *pui) {
    int i;

    for(i = 0; i < 3; i++)
        if((guint) def_group_type == group_type[i]) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->group_type[i]), TRUE);
            break;
        }

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->offsets_col), show_offsets_column);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->undo_spin), (gfloat)max_undo_depth);

    gtk_entry_set_text(GTK_ENTRY(pui->font_entry), def_font_name ? def_font_name : "");

    gtk_widget_set_sensitive(pui->format, FALSE);
    gtk_entry_set_text(GTK_ENTRY(pui->format), offset_fmt);
    if(strcmp(offset_fmt, "%d") == 0)
        gtk_option_menu_set_history(GTK_OPTION_MENU(pui->offset_menu), 0);
    else if(strcmp(offset_fmt, "%x") == 0 || strcmp(offset_fmt, "%X") == 0)
        gtk_option_menu_set_history(GTK_OPTION_MENU(pui->offset_menu), 1);
    else {
        gtk_option_menu_set_history(GTK_OPTION_MENU(pui->offset_menu), 2);
        gtk_widget_set_sensitive(pui->format, TRUE);
    }

    set_modified(pui, FALSE);
}

/*
 * callbacks for properties dialog and its widgets
 */
static void properties_modified_cb(GtkWidget *w G_GNUC_UNUSED, PropertyUI *pui) {
    set_modified(pui, TRUE);
}

static void max_undo_changed_cb(GtkAdjustment *adj, PropertyUI *pui) {
    if((guint)adj->value != max_undo_depth)
        set_modified(pui, TRUE);
}

static void font_selected_cb(GtkWidget *w G_GNUC_UNUSED, GtkFontSelectionDialog *fsd) {
    PropertyUI *pui = gtk_object_get_user_data(GTK_OBJECT(fsd));
    gchar *name;

    if((name = gtk_font_selection_dialog_get_font_name(fsd)) != NULL) {
        gtk_entry_set_text(GTK_ENTRY(pui->font_entry), name);
        g_free(name);
    }

    gtk_widget_destroy(GTK_WIDGET(fsd));
}

static void browse_font_cb(GtkWidget *w G_GNUC_UNUSED, PropertyUI *pui) {
    static gchar *spacings[] = { "m", "c", NULL };
    GtkWidget *fsd;

    fsd = gtk_font_selection_dialog_new(_("Select font"));
    gtk_object_set_user_data(GTK_OBJECT(fsd), pui);
    gtk_window_set_modal(GTK_WINDOW(fsd), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(fsd), GTK_WINDOW(pui->pbox));
    gtk_window_position(GTK_WINDOW(fsd), GTK_WIN_POS_MOUSE);

    gtk_font_selection_dialog_set_filter(GTK_FONT_SELECTION_DIALOG(fsd),
                                         GTK_FONT_FILTER_BASE, GTK_FONT_ALL,
                                         NULL, NULL, NULL, NULL, spacings, NULL);
    gtk_font_selection_dialog_set_font_name(GTK_FONT_SELECTION_DIALOG(fsd),
                                            gtk_entry_get_text(GTK_ENTRY(pui->font_entry)));
    gtk_font_selection_dialog_set_preview_text(GTK_FONT_SELECTION_DIALOG(fsd),
                                               "00 1F A0 FF  0123456789ABCDEF");

    gtk_signal_connect(GTK_OBJECT(GTK_FONT_SELECTION_DIALOG(fsd)->ok_button), "clicked",
                       GTK_SIGNAL_FUNC(font_selected_cb), fsd);
    gtk_signal_connect_object(GTK_OBJECT(GTK_FONT_SELECTION_DIALOG(fsd)->cancel_button),
                              "clicked", GTK_SIGNAL_FUNC(gtk_widget_destroy),
                              GTK_OBJECT(fsd));

    gtk_widget_show(fsd);
}

static void for_each_view(void (*func)(GtkHex *, gpointer), gpointer data) {
    GList *child, *view;

    for(child = get_documents(); child; child = child->next)
        for(view = HEX_DOCUMENT(child->data)->views; view; view = view->next)
            func(GTK_HEX(view->data), data);
}

static void show_offsets_func(GtkHex *view, gpointer data) {
    gtk_hex_show_offsets(view, GPOINTER_TO_INT(data));
}

static void set_font_func(GtkHex *view, gpointer data) {
    gtk_hex_set_font(view, (GdkFont *)data);
}

/* FALSE, with nothing changed, for settings that cannot be used */
static gboolean apply_changes(PropertyUI *pui) {
    GList *child;
    GdkFont *new_font = NULL;
    guint new_undo_max;
    gboolean show_off;
    gchar *new_fmt, *new_font_name;
    gint bad_pos;
    int i;

    new_fmt = gtk_entry_get_text(GTK_ENTRY(pui->format));
    if(!offset_format_valid(new_fmt, &bad_pos)) {
        gtk_entry_select_region(GTK_ENTRY(pui->format), bad_pos, bad_pos + 1);
        show_error(_("The offset format string contains format specifier other than 'x', 'd' or 'o'!"));
        gtk_window_set_focus(GTK_WINDOW(pui->pbox), pui->format);
        return FALSE;
    }

    new_font_name = gtk_entry_get_text(GTK_ENTRY(pui->font_entry));
    if(!def_font_name || strcmp(new_font_name, def_font_name) != 0) {
        if((new_font = gdk_font_load(new_font_name)) == NULL) {
            show_error(_("Can not open desired font!"));
            return FALSE;
        }
    }

    g_free(offset_fmt);
    offset_fmt = g_strdup(new_fmt);
    view_changed();

    show_off = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->offsets_col));
    if(show_off != show_offsets_column) {
        show_offsets_column = show_off;
        for_each_view(show_offsets_func, GINT_TO_POINTER(show_off));
    }

    for(i = 0; i < 3; i++)
        if(GTK_TOGGLE_BUTTON(pui->group_type[i])->active) {
            def_group_type = group_type[i];
            break;
        }

    new_undo_max = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(pui->undo_spin));
    if(new_undo_max != max_undo_depth) {
        max_undo_depth = new_undo_max;

        for(child = get_documents(); child; child = child->next)
            hex_document_set_max_undo(HEX_DOCUMENT(child->data), max_undo_depth);
    }

    if(new_font) {
        for_each_view(set_font_func, new_font);

        if(def_font)
            gdk_font_unref(def_font);
        def_font = new_font;

        g_free(def_font_name);
        def_font_name = g_strdup(new_font_name);
    }

    save_configuration();

    return TRUE;
}

static void offset_cb(GtkWidget *w, PropertyUI *pui) {
    int i;

    for(i = 0; i < 3; i++)
        if(w == pui->offset_choice[i])
            break;

    switch(i) {
    case 0:
        gtk_entry_set_text(GTK_ENTRY(pui->format), "%d");
        gtk_widget_set_sensitive(pui->format, FALSE);
        break;
    case 1:
        gtk_entry_set_text(GTK_ENTRY(pui->format), "%X");
        gtk_widget_set_sensitive(pui->format, FALSE);
        break;
    case 2:
        gtk_widget_set_sensitive(pui->format, TRUE);
        break;
    }

    properties_modified_cb(w, pui);
}
