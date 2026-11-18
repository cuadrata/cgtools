/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* converter.c - conversion dialog

   Copyright (C) 1998, 1999 Free Software Foundation

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
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include <gdk/gdkkeysyms.h>

#include "cghex.h"

static void conv_entry_cb(GtkEntry *, gpointer);
static void get_cursor_val_cb(GtkButton *button, Converter *conv);
static void set_values(Converter *conv, gulong val);

Converter *converter = NULL;

/* The bases of the entries, in their order; 0 is the text. */
static const gint entry_base[5] = { 2, 8, 10, 16, 0 };

static gboolean is_char_ok(gchar c, gint base)
{
    gint digit;

    if(base == 0)
        return TRUE;
    if(c >= '0' && c <= '9')
        digit = c - '0';
    else if(c >= 'a' && c <= 'f')
        digit = c - 'a' + 10;
    else if(c >= 'A' && c <= 'F')
        digit = c - 'A' + 10;
    else
        return FALSE;

    return digit < base;
}

/* Only the digits of the entry's base go in: a bad one is refused with a beep. */
static void entry_filter(GtkEditable *editable, const gchar *text, gint length,
                         gint *pos G_GNUC_UNUSED, gpointer data)
{
    gint base = GPOINTER_TO_INT(data);
    gint i;

    for(i = 0; i < length; i++)
        if(!is_char_ok(text[i], base)) {
            gdk_beep();
            gtk_signal_emit_stop_by_name(GTK_OBJECT(editable), "insert_text");
            return;
        }
}

/* Alt+letter goes to the accelerators, not the entry. */
static gint entry_key_press_cb(GtkWidget *widget, GdkEventKey *event, gpointer data G_GNUC_UNUSED)
{
    if(event->state & GDK_MOD1_MASK)
        gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event");

    return FALSE;
}

static GtkWidget *create_converter_entry(const gchar *name, GtkWidget *table,
                                         GtkAccelGroup *accel_group, gint pos, gint base)
{
    GtkWidget *label, *entry;
    guint accel_key;

    label = gtk_label_new("");
    accel_key = gtk_label_parse_uline(GTK_LABEL(label), name);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, pos, pos + 1);
    gtk_widget_show(label);

    entry = gtk_entry_new();
    gtk_signal_connect(GTK_OBJECT(entry), "activate",
                       GTK_SIGNAL_FUNC(conv_entry_cb), GINT_TO_POINTER(base));
    gtk_signal_connect(GTK_OBJECT(entry), "key_press_event",
                       GTK_SIGNAL_FUNC(entry_key_press_cb), NULL);
    gtk_signal_connect(GTK_OBJECT(entry), "insert_text",
                       GTK_SIGNAL_FUNC(entry_filter), GINT_TO_POINTER(base));
    if(accel_key != GDK_VoidSymbol)
        gtk_widget_add_accelerator(entry, "grab_focus", accel_group, accel_key,
                                   GDK_MOD1_MASK, GTK_ACCEL_VISIBLE);
    gtk_table_attach_defaults(GTK_TABLE(table), entry, 1, 2, pos, pos + 1);
    gtk_widget_show(entry);

    return entry;
}

static GtkWidget *create_converter_button(const gchar *name, GtkAccelGroup *accel_group)
{
    GtkWidget *button, *label;
    guint accel_key;

    button = gtk_button_new();
    label = gtk_label_new("");
    accel_key = gtk_label_parse_uline(GTK_LABEL(label), name);
    gtk_misc_set_alignment(GTK_MISC(label), 0.5, 0.5);
    gtk_container_add(GTK_CONTAINER(button), label);
    if(accel_key != GDK_VoidSymbol)
        gtk_widget_add_accelerator(button, "clicked", accel_group, accel_key,
                                   GDK_MOD1_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_show(label);
    gtk_widget_show(button);

    return button;
}

Converter *create_converter(void) {
    static const gchar *names[5] = {
        N_("_Binary"), N_("_Octal"), N_("_Decimal"), N_("_Hex"), N_("_ASCII")
    };
    Converter *conv;
    GtkWidget *table;
    GtkAccelGroup *accel_group;
    gint i;

    conv = g_new0(Converter, 1);

    conv->window = gtk_dialog_new();
    gtk_signal_connect(GTK_OBJECT(conv->window), "delete_event",
                       GTK_SIGNAL_FUNC(delete_event_cb), conv->window);
    gtk_window_set_title(GTK_WINDOW(conv->window), _("CGHex: Converter"));

    accel_group = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(conv->window), accel_group);

    table = gtk_table_new(5, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), CGHEX_PAD_SMALL);
    gtk_table_set_col_spacings(GTK_TABLE(table), CGHEX_PAD_SMALL);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(conv->window)->vbox), table,
                       TRUE, TRUE, 0);
    gtk_widget_show(table);

    for(i = 0; i < 5; i++)
        conv->entry[i] = create_converter_entry(_(names[i]), table, accel_group,
                                                i, entry_base[i]);

    conv->get = create_converter_button(_("_Get cursor value"), accel_group);
    gtk_signal_connect(GTK_OBJECT(conv->get), "clicked",
                       GTK_SIGNAL_FUNC(get_cursor_val_cb), conv);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(conv->window)->vbox), conv->get,
                       FALSE, TRUE, 0);

    conv->close = create_converter_button(_("_Close"), accel_group);
    gtk_signal_connect(GTK_OBJECT(conv->close), "clicked",
                       GTK_SIGNAL_FUNC(cancel_cb), conv->window);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(conv->window)->action_area), conv->close,
                       TRUE, TRUE, 0);

    gtk_container_border_width(GTK_CONTAINER(GTK_DIALOG(conv->window)->vbox), 2);
    gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(conv->window)->vbox), 2);

    return conv;
}

static void get_cursor_val_cb(GtkButton *button G_GNUC_UNUSED, Converter *conv) {
    GtkHex *view = get_active_view();
    guint val, start;

    if(view) {
        start = gtk_hex_get_cursor(view);
        start = start - start % view->group_type;
        val = 0;
        do {
            val <<= 8;
            val |= gtk_hex_get_byte(view, start);
            start++;
        } while( (start % view->group_type != 0) &&
                 (start < view->document->file_size) );

        set_values(conv, val);
    }
}

static void set_values(Converter *conv, gulong val) {
    gchar buffer[33], *text;
    GdkWChar ascii[5];
    guchar c;
    gint i;

    val &= 0xFFFFFFFFUL;
    conv->value = val;

    for(i = 0; i < 32; i++)
        buffer[i] = ((val & (1UL << (31 - i)))?'1':'0');
    buffer[i] = 0;
    gtk_entry_set_text(GTK_ENTRY(conv->entry[0]), buffer);

    g_snprintf(buffer, sizeof buffer, "%011lo", val);
    gtk_entry_set_text(GTK_ENTRY(conv->entry[1]), buffer);

    g_snprintf(buffer, sizeof buffer, "%lu", val);
    gtk_entry_set_text(GTK_ENTRY(conv->entry[2]), buffer);

    g_snprintf(buffer, sizeof buffer, "%08lX", val);
    gtk_entry_set_text(GTK_ENTRY(conv->entry[3]), buffer);

    /* one character a byte, the way the views show them */
    for(i = 0; i < 4; i++) {
        GdkWChar wc;

        c = (val >> (3 - i)*8) & 0xFF;
        if(c < ' ' || (c >= 0x7F && c < 0xA0))
            wc = '_';
        else
            wc = c;
        ascii[i] = wc;
    }
    ascii[i] = 0;
    text = gdk_wcstombs(ascii);
    gtk_entry_set_text(GTK_ENTRY(conv->entry[4]), text ? text : "");
    g_free(text);
}

static void conv_entry_cb(GtkEntry *entry, gpointer data) {
    gint base = GPOINTER_TO_INT(data);
    gchar buffer[33];
    gchar *text, *endptr;
    gulong val = converter->value;
    GdkWChar wc[5];
    int i, len;

    text = gtk_entry_get_text(entry);

    switch(base) {
    case 0:
        /* the characters, one byte each, as they are shown */
        len = gdk_mbstowcs(wc, text, 4);
        for(val = 0, i = 0; i < len; i++) {
            val <<= 8;
            val |= wc[i] & 0xFF;
        }
        break;
    case 2:
        strncpy(buffer, text, 32);
        buffer[32] = 0;
        break;
    case 8:
        strncpy(buffer, text, 11);
        buffer[11] = 0;
        break;
    case 10:
        strncpy(buffer, text, 10);
        buffer[10] = 0;
        break;
    case 16:
        strncpy(buffer, text, 8);
        buffer[8] = 0;
        break;
    }

    if(base != 0) {
        val = strtoul(buffer, &endptr, base);
        if(*endptr != 0) {
            /* not a number of that base: the others are cleared, it is left to fix */
            converter->value = 0;
            for(i = 0; i < 5; i++)
                if(GTK_ENTRY(converter->entry[i]) != entry)
                    gtk_entry_set_text(GTK_ENTRY(converter->entry[i]), "");
            gtk_entry_select_region(entry, 0, -1);
            return;
        }
    }

    if(val == converter->value)
        return;

    set_values(converter, val);
}
