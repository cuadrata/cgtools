/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* hex-document-ui.c - callbacks for the menus that act on a document

   Copyright (C) 2000 Free Software Foundation

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

#include <gtk/gtk.h>
#include <gdk/gdk.h>

#include "cghex.h"
#include "hex-document.h"
#include "gtkhex.h"

void hex_document_set_menu_sensitivity(HexDocument *doc)
{
    if(doc == NULL || doc != get_active_document())
        return;

    set_menu_sensitive("/Edit/Undo", doc->undo_top != NULL);
    set_menu_sensitive("/Edit/Redo",
                       doc->undo_stack && doc->undo_top != doc->undo_stack);
}

/*
 * callbacks for document's menus
 */
static void set_group_type(GtkWidget *w, guint gt)
{
    GtkHex *view;

    if(menu_callbacks_blocked || !GTK_CHECK_MENU_ITEM(w)->active)
        return;

    if((view = get_active_view()) != NULL)
        gtk_hex_set_group_type(view, gt);
}

void set_byte_cb(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *w)
{
    set_group_type(w, GROUP_BYTE);
}

void set_word_cb(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *w)
{
    set_group_type(w, GROUP_WORD);
}

void set_long_cb(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *w)
{
    set_group_type(w, GROUP_LONG);
}

void insert_cb(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *w)
{
    GtkHex *view;

    if(menu_callbacks_blocked)
        return;

    if((view = get_active_view()) != NULL)
        gtk_hex_set_insert_mode(view, GTK_CHECK_MENU_ITEM(w)->active);
}

void find_cb(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *w G_GNUC_UNUSED)
{
    if(!find_dialog)
        find_dialog = create_find_dialog();

    if(!GTK_WIDGET_VISIBLE(find_dialog->window))
        gtk_window_set_default(GTK_WINDOW(find_dialog->window), find_dialog->f_next);
    present_window(find_dialog->window);
}

void replace_cb(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *w G_GNUC_UNUSED)
{
    if(!replace_dialog)
        replace_dialog = create_replace_dialog();

    if(!GTK_WIDGET_VISIBLE(replace_dialog->window))
        gtk_window_set_default(GTK_WINDOW(replace_dialog->window), replace_dialog->next);
    present_window(replace_dialog->window);
}

void jump_cb(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *w G_GNUC_UNUSED)
{
    if(!jump_dialog)
        jump_dialog = create_jump_dialog();

    if(!GTK_WIDGET_VISIBLE(jump_dialog->window))
        gtk_window_set_default(GTK_WINDOW(jump_dialog->window), jump_dialog->ok);
    present_window(jump_dialog->window);
}

void undo_cb(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *w G_GNUC_UNUSED)
{
    GtkHex *view;
    HexDocument *doc;
    HexChangeData *cd;

    if((view = get_active_view()) == NULL)
        return;
    doc = view->document;

    if(doc->undo_top) {
        cd = (HexChangeData *)doc->undo_top->data;

        hex_document_undo(doc);

        gtk_hex_set_cursor(view, cd->start);
        gtk_hex_set_nibble(view, cd->lower_nibble);
    }
}

void redo_cb(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *w G_GNUC_UNUSED)
{
    GtkHex *view;
    HexDocument *doc;
    HexChangeData *cd;

    if((view = get_active_view()) == NULL)
        return;
    doc = view->document;

    if(doc->undo_stack && doc->undo_top != doc->undo_stack) {
        hex_document_redo(doc);

        cd = (HexChangeData *)doc->undo_top->data;

        gtk_hex_set_cursor(view, cd->start);
        gtk_hex_set_nibble(view, cd->lower_nibble);
    }
}

void add_view_cb(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *w G_GNUC_UNUSED)
{
    HexDocument *doc;

    if((doc = get_active_document()) != NULL)
        add_view(doc);
}

void remove_view_cb(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *w G_GNUC_UNUSED)
{
    GtkHex *view;

    if((view = get_active_view()) != NULL)
        remove_view(view);
}
