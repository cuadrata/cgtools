/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* findreplace.c - finding & replacing data

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

static void find_next_cb(GtkWidget *, gpointer);
static void find_prev_cb(GtkWidget *, gpointer);
static void replace_next_cb(GtkWidget *, gpointer);
static void replace_one_cb(GtkWidget *, gpointer);
static void replace_all_cb(GtkWidget *, gpointer);
static void set_find_type_cb(GtkWidget *, gpointer);
static void set_replace_type_cb(GtkWidget *, gpointer);
static void goto_byte_cb(GtkWidget *, gpointer);
static gint get_search_string(gchar *, guchar *, gint);

/* the most bytes searched for; the buffers are one longer */
#define SEARCH_STRING_MAX 255

/* the buttons of a dialog, packed as GnomeDialog packed them */
static GtkWidget *dialog_button(GtkWidget *dialog, const gchar *label,
                                GtkSignalFunc func, gpointer data)
{
    GtkWidget *button;

    button = gtk_button_new_with_label(label);
    gtk_signal_connect(GTK_OBJECT(button), "clicked", func, data);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area), button,
                       TRUE, TRUE, 0);
    GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
    gtk_widget_show(button);

    return button;
}

FindDialog *find_dialog = NULL;
ReplaceDialog *replace_dialog = NULL;
JumpDialog *jump_dialog = NULL;

FindDialog *create_find_dialog(void)
{
    gint i;
    GSList *group;
    gchar type_label[256];
    FindDialog *dialog;

    dialog = g_new0(FindDialog, 1);

    dialog->window = gtk_dialog_new();
    gtk_signal_connect(GTK_OBJECT(dialog->window), "delete_event",
                       GTK_SIGNAL_FUNC(delete_event_cb), dialog->window);

    create_dialog_title(dialog->window, _("CGHex (%s): Find Data"));

    dialog->f_string = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog->window)->vbox), dialog->f_string,
                       TRUE, TRUE, 0);
    gtk_widget_show(dialog->f_string);

    for(i = 0, group = NULL; i < 2;
        i++, group = gtk_radio_button_group(GTK_RADIO_BUTTON(dialog->type_button[i-1]))) {
        sprintf(type_label, _("Search for %s"), _(search_type_label[i]));

        dialog->type_button[i] = gtk_radio_button_new_with_label(group, type_label);

        if(dialog->search_type == i)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->type_button[i]), TRUE);

        gtk_signal_connect(GTK_OBJECT(dialog->type_button[i]), "clicked",
                           GTK_SIGNAL_FUNC(set_find_type_cb), GINT_TO_POINTER(i));

        gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog->window)->vbox), dialog->type_button[i],
                           TRUE, TRUE, 0);

        gtk_widget_show(dialog->type_button[i]);
    }

    dialog->f_next = dialog_button(dialog->window, _("Find Next"),
                                   GTK_SIGNAL_FUNC(find_next_cb), dialog->f_string);
    dialog->f_prev = dialog_button(dialog->window, _("Find Previous"),
                                   GTK_SIGNAL_FUNC(find_prev_cb), dialog->f_string);
    dialog->f_close = dialog_button(dialog->window, _("Close"),
                                    GTK_SIGNAL_FUNC(cancel_cb), dialog->window);

    gtk_container_border_width(GTK_CONTAINER(GTK_DIALOG(dialog->window)->vbox), 2);
    gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(dialog->window)->vbox), 2);

    return dialog;
}

ReplaceDialog *create_replace_dialog(void)
{
    gint i;
    GSList *group;
    gchar type_label[256];
    ReplaceDialog *dialog;

    dialog = g_new0(ReplaceDialog, 1);

    dialog->window = gtk_dialog_new();
    gtk_signal_connect(GTK_OBJECT(dialog->window), "delete_event",
                       GTK_SIGNAL_FUNC(delete_event_cb), dialog->window);

    create_dialog_title(dialog->window, _("CGHex (%s): Find & Replace Data"));

    dialog->f_string = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog->window)->vbox), dialog->f_string,
                       TRUE, TRUE, 0);
    gtk_widget_show(dialog->f_string);

    dialog->r_string = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog->window)->vbox), dialog->r_string,
                       TRUE, TRUE, 0);
    gtk_widget_show(dialog->r_string);

    for(i = 0, group = NULL; i < 2;
        i++, group = gtk_radio_button_group(GTK_RADIO_BUTTON(dialog->type_button[i-1]))) {
        sprintf(type_label, _("Replace %s"), _(search_type_label[i]));

        dialog->type_button[i] = gtk_radio_button_new_with_label(group, type_label);

        if(dialog->search_type == i)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialog->type_button[i]), TRUE);

        gtk_signal_connect(GTK_OBJECT(dialog->type_button[i]), "clicked",
                           GTK_SIGNAL_FUNC(set_replace_type_cb), GINT_TO_POINTER(i));

        gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog->window)->vbox), dialog->type_button[i],
                           TRUE, TRUE, 0);

        gtk_widget_show(dialog->type_button[i]);
    }

    dialog->next = dialog_button(dialog->window, _("Find next"),
                                 GTK_SIGNAL_FUNC(replace_next_cb), dialog->f_string);
    dialog->replace = dialog_button(dialog->window, _("Replace"),
                                    GTK_SIGNAL_FUNC(replace_one_cb), NULL);
    dialog->replace_all = dialog_button(dialog->window, _("Replace All"),
                                        GTK_SIGNAL_FUNC(replace_all_cb), NULL);
    dialog->close = dialog_button(dialog->window, _("Close"),
                                  GTK_SIGNAL_FUNC(cancel_cb), dialog->window);

    gtk_container_border_width(GTK_CONTAINER(GTK_DIALOG(dialog->window)->vbox), 2);
    gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(dialog->window)->vbox), 2);

    return dialog;
}

JumpDialog *create_jump_dialog(void)
{
    JumpDialog *dialog;

    dialog = g_new0(JumpDialog, 1);

    dialog->window = gtk_dialog_new();
    gtk_signal_connect(GTK_OBJECT(dialog->window), "delete_event",
                       GTK_SIGNAL_FUNC(delete_event_cb), dialog->window);

    create_dialog_title(dialog->window, _("CGHex (%s): Jump To Byte"));

    dialog->int_entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog->window)->vbox), dialog->int_entry,
                       TRUE, TRUE, 0);
    gtk_widget_show(dialog->int_entry);

    dialog->ok = dialog_button(dialog->window, _("OK"),
                               GTK_SIGNAL_FUNC(goto_byte_cb), dialog->int_entry);
    dialog->cancel = dialog_button(dialog->window, _("Cancel"),
                                   GTK_SIGNAL_FUNC(cancel_cb), dialog->window);

    gtk_container_border_width(GTK_CONTAINER(GTK_DIALOG(dialog->window)->vbox), 2);
    gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(dialog->window)->vbox), 2);

    return dialog;
}

static gint get_search_string(gchar *str, guchar *buf, gint type)
{
    gint len = strlen(str), shift;

    if(len > 0) {
        if(type == DATA_TYPE_HEX) {
            /* we convert the string from hex */
            if(len % 2 != 0 || len / 2 > SEARCH_STRING_MAX)
                return 0;  /* the number of hex digits must be EVEN */
            len = 0;     /* we'll store the returned string length in len */
            shift = 4;
            *buf = '\0';
            while(*str != 0) {
                if((*str >= '0') && (*str <= '9'))
                    *buf |= (*str - '0') << shift;
                else if((*str >= 'A') && (*str <= 'F'))
                    *buf |= (*str - 'A' + 10) << shift;
                else if((*str >= 'a') && (*str <= 'f'))
                    *buf |= (*str - 'a' + 10) << shift;
                else
                    return 0;

                if(shift > 0)
                    shift = 0;
                else {
                    shift = 4;
                    buf++;
                    len++;
                    *buf = '\0';
                }

                str++;
            }
        }
        else if(type == DATA_TYPE_ASCII) {
            if(len > SEARCH_STRING_MAX)
                return 0;
            memcpy(buf, str, len);
        }

        return len;
    }
    return 0;
}

static void set_find_type_cb(GtkWidget *w, gpointer type)
{
    if(GTK_TOGGLE_BUTTON(w)->active)
        find_dialog->search_type = GPOINTER_TO_INT(type);
}

static void set_replace_type_cb(GtkWidget *w, gpointer type)
{
    if(GTK_TOGGLE_BUTTON(w)->active)
        replace_dialog->search_type = GPOINTER_TO_INT(type);
}

static void find_next_cb(GtkWidget *w G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
    GtkHex *gh;
    guint offset, str_len;
    guchar str[256];

    if((gh = get_active_view()) == NULL) {
        show_error(_("There is no active buffer to search!"));
        return;
    }

    if((str_len = get_search_string(gtk_entry_get_text(GTK_ENTRY(find_dialog->f_string)), str,
                                    find_dialog->search_type)) == 0) {
        show_error(_("There seems to be no string to search for!"));
        return;
    }
    if(hex_document_find_forward(gh->document,
                                 gh->cursor_pos+1, str, str_len, &offset))
        gtk_hex_set_cursor(gh, offset);
    else
        flash_status(_("End Of File reached"));
}

static void find_prev_cb(GtkWidget *w G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
    GtkHex *gh;
    guint offset, str_len;
    guchar str[256];

    if((gh = get_active_view()) == NULL) {
        show_error(_("There is no active buffer to search!"));
        return;
    }

    if((str_len = get_search_string(gtk_entry_get_text(GTK_ENTRY(find_dialog->f_string)), str,
                                    find_dialog->search_type)) == 0) {
        show_error(_("There seems to be no string to search for!"));
        return;
    }

    if(hex_document_find_backward(gh->document,
                                  gh->cursor_pos, str, str_len, &offset))
        gtk_hex_set_cursor(gh, offset);
    else
        flash_status(_("Beginning Of File reached"));
}

static void goto_byte_cb(GtkWidget *w G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
    guint byte = 2;
    gchar *byte_str = gtk_entry_get_text(GTK_ENTRY(jump_dialog->int_entry));
    GtkHex *gh;

    if((gh = get_active_view()) == NULL) {
        show_error(_("There is no active buffer to move the cursor in!"));
        return;
    }

    if(strlen(byte_str) == 0) {
        show_error(_("No offset has been specified!"));
        return;
    }

    if((sscanf(byte_str, "0x%x", &byte) == 1) ||
       (sscanf(byte_str, "%u", &byte) == 1)) {
        if(byte >= gh->document->file_size)
            show_error(_("Can not position cursor beyond the End Of File!"));
        else
            gtk_hex_set_cursor(gh, byte);
    }
    else
        show_error(_("The offset must be a positive integer value!"));
}

static void replace_next_cb(GtkWidget *w G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
    GtkHex *gh;
    guint offset, str_len;
    guchar str[256];

    if((gh = get_active_view()) == NULL) {
        show_error(_("There is no active buffer to search!"));
        return;
    }

    if((str_len = get_search_string(gtk_entry_get_text(GTK_ENTRY(replace_dialog->f_string)), str,
                                    replace_dialog->search_type)) == 0) {
        show_error(_("There seems to be no string to search for!"));
        return;
    }

    if(hex_document_find_forward(gh->document,
                                 gh->cursor_pos+1, str, str_len, &offset))
        gtk_hex_set_cursor(gh, offset);
    else
        flash_status(_("End Of File reached"));
}

static void replace_one_cb(GtkWidget *w G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
    guchar find_str[256], rep_str[256];
    guint find_len, rep_len, offset;
    GtkHex *gh;
    HexDocument *doc;

    if((gh = get_active_view()) == NULL) {
        show_error(_("There is no active buffer to replace data in!"));
        return;
    }

    doc = gh->document;

    if( ((find_len = get_search_string(gtk_entry_get_text(GTK_ENTRY(replace_dialog->f_string)), find_str,
                                       replace_dialog->search_type)) == 0) ||
        ((rep_len = get_search_string(gtk_entry_get_text(GTK_ENTRY(replace_dialog->r_string)), rep_str,
                                      replace_dialog->search_type)) == 0)) {
        show_error(_("Strange find or replace string!"));
        return;
    }

    if(find_len > doc->file_size - gh->cursor_pos)
        return;

    if(hex_document_compare_data(doc, find_str, gh->cursor_pos, find_len) == 0)
        hex_document_set_data(doc, gh->cursor_pos,
                              rep_len, find_len, rep_str, TRUE);

    if(hex_document_find_forward(doc, gh->cursor_pos + rep_len, find_str, find_len,
                                 &offset))
        gtk_hex_set_cursor(gh, offset);
    else
        flash_status(_("End Of File reached!"));
}

static void replace_all_cb(GtkWidget *w G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
    guchar find_str[256], rep_str[256];
    gchar *flash;
    guint find_len, rep_len, offset, count, pos;
    GtkHex *gh;
    HexDocument *doc;

    if((gh = get_active_view()) == NULL) {
        show_error(_("There is no active buffer to replace data in!"));
        return;
    }

    doc = gh->document;

    if( ((find_len = get_search_string(gtk_entry_get_text(GTK_ENTRY(replace_dialog->f_string)), find_str,
                                       replace_dialog->search_type)) == 0) ||
        ((rep_len = get_search_string(gtk_entry_get_text(GTK_ENTRY(replace_dialog->r_string)), rep_str,
                                      replace_dialog->search_type)) == 0)) {
        show_error(_("Strange find or replace string!"));
        return;
    }

    if(find_len > doc->file_size - gh->cursor_pos)
        return;

    count = 0;
    pos = gh->cursor_pos;
    while(hex_document_find_forward(doc, pos, find_str, find_len,
                                    &offset)) {
        hex_document_set_data(doc, offset, rep_len, find_len, rep_str, TRUE);
        count++;
        /* on past what was put in, which may hold what is looked for */
        pos = offset + rep_len;
    }

    if(count > 0)
        gtk_hex_set_cursor(gh, MIN(pos, doc->file_size));

    flash = g_strdup_printf(_("Replaced %d occurrences."), count);
    flash_status(flash);
    g_free(flash);
}
