/* gtkeditor.h - a widget that edits plain text

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

#ifndef GTK_EDITOR_H
#define GTK_EDITOR_H

#include <gtk/gtk.h>

#include "textbuf.h"

#define GTK_TYPE_EDITOR            (gtk_editor_get_type())
#define GTK_EDITOR(obj)            (GTK_CHECK_CAST((obj), GTK_TYPE_EDITOR, GtkEditor))
#define GTK_EDITOR_CLASS(klass)    (GTK_CHECK_CLASS_CAST((klass), GTK_TYPE_EDITOR, GtkEditorClass))
#define GTK_IS_EDITOR(obj)         (GTK_CHECK_TYPE((obj), GTK_TYPE_EDITOR))

typedef struct _GtkEditor GtkEditor;
typedef struct _GtkEditorClass GtkEditorClass;

/*
 * A line as it is laid out: its characters, where each starts in the
 * text, how wide each is and where it stands across its row, and
 * which character each of its rows starts with. A line that is not
 * wrapped is one row.
 */
typedef struct {
    GdkWChar *ch;
    gsize *at;      /* n + 1 of them: the last is where the line ends */
    gint *x, *w;
    gint *row;      /* rows + 1 of them: the last is n */
    gint n, rows, size, row_size;
} GtkEditorLine;

/*
 * Plain text in one font, as an editor has it: UTF-8, with a cursor
 * and a selection, undone and redone without end, wrapped at words or
 * scrolled sideways, with its lines numbered if that is wanted. Places
 * in the text are counted in bytes from its start.
 *
 * The keys are the ordinary ones, and the widget takes a key only if
 * it has a use for it, so that a window's menus get the rest. A
 * program that wants a key for itself connects to key_press_event,
 * which is heard before the widget hears it.
 */
struct _GtkEditor {
    GtkWidget widget;

    TextBuf *text;
    GtkAdjustment *hadj, *vadj;
    GdkFont *font;        /* NULL for the style's */
    GtkIMContext *im;
    GdkGC *gc;
    GtkWidget *menu;

    gsize cursor, anchor; /* what is selected lies between them */
    gint want_x;          /* where across the cursor keeps to, going up and down; -1 if nowhere yet */
    gint tab_width;       /* in spaces */
    guint wrap : 1;
    guint numbers : 1;
    guint auto_indent : 1;
    guint overwrite : 1;
    guint stale : 1;      /* the text has changed, and nobody has been told */
    guint selecting : 1;  /* the button is down and the selection follows the pointer */
    guint may_drag : 1;   /* it went down on what is selected, which may be dragged away */
    guint dragging : 1;
    guint clicks;

    /* How it is laid out. */
    gint line_h, ascent, space_w, margin, max_w, wrap_w;
    gint ascii_w[128];
    struct {
        GdkWChar c;
        gint w;
    } wide_w[256];
    guint *rows;          /* how many rows each line takes, when lines are wrapped */
    gsize *first;         /* the row each line starts on; good for the first first_ok lines */
    gsize first_ok, rows_size;
    GtkEditorLine laid;   /* the line last laid out */
    gsize laid_line;
    guint laid_stamp, stamp;

    GdkWChar *mark;       /* what is shown up wherever it is, if anything */
    gint mark_n;
    gboolean mark_case;

    gchar *copied;        /* what the clipboard has of ours */
    gchar *preedit;       /* what an input method is in the middle of */
    gint press_x, press_y;
    guint scroller;
};

struct _GtkEditorClass {
    GtkWidgetClass parent_class;

    void (*set_scroll_adjustments)(GtkEditor *editor, GtkAdjustment *hadj, GtkAdjustment *vadj);
    void (*changed)(GtkEditor *editor);  /* the text is other than it was */
    void (*moved)(GtkEditor *editor);    /* the cursor or the selection is */
    void (*files_dropped)(GtkEditor *editor, const gchar *uris);  /* as a text/uri-list has them */
};

GtkType gtk_editor_get_type(void);
GtkWidget *gtk_editor_new(void);

/* The text. What is given need not be valid UTF-8, and is made so. */
void gtk_editor_set_text(GtkEditor *editor, const gchar *text, gssize len);
gchar *gtk_editor_get_text(GtkEditor *editor, gsize start, gsize end);
gsize gtk_editor_get_length(GtkEditor *editor);
gsize gtk_editor_get_lines(GtkEditor *editor);
gsize gtk_editor_line_of(GtkEditor *editor, gsize pos);
gsize gtk_editor_line_start(GtkEditor *editor, gsize line);

/* The cursor and the selection. To select is to bring into view. */
gsize gtk_editor_get_cursor(GtkEditor *editor);
gboolean gtk_editor_get_selection(GtkEditor *editor, gsize *start, gsize *end);
void gtk_editor_select(GtkEditor *editor, gsize start, gsize end);
void gtk_editor_select_all(GtkEditor *editor);

/* Changes, as if the user had made them: they can be undone. */
void gtk_editor_insert(GtkEditor *editor, const gchar *text, gssize len);
void gtk_editor_delete_selection(GtkEditor *editor);
void gtk_editor_cut(GtkEditor *editor);
void gtk_editor_copy(GtkEditor *editor);
void gtk_editor_paste(GtkEditor *editor);
void gtk_editor_begin_action(GtkEditor *editor);
void gtk_editor_end_action(GtkEditor *editor);
gboolean gtk_editor_can_undo(GtkEditor *editor);
gboolean gtk_editor_can_redo(GtkEditor *editor);
void gtk_editor_undo(GtkEditor *editor);
void gtk_editor_redo(GtkEditor *editor);

/* Whether the text is other than when it was set or last said to be saved. */
gboolean gtk_editor_get_modified(GtkEditor *editor);
void gtk_editor_set_saved(GtkEditor *editor);

/*
 * Looking for something from the cursor on, or back, and round from
 * the other end if need be: what is found is selected. Making every
 * place it is into something else, as one thing to undo: how many
 * there were. And showing up every place something is, until the user
 * does something else; NULL shows nothing up.
 */
gboolean gtk_editor_find(GtkEditor *editor, const gchar *what, gboolean forward, gboolean match_case);
gint gtk_editor_replace_all(GtkEditor *editor, const gchar *what, const gchar *with, gboolean match_case);
void gtk_editor_set_highlight(GtkEditor *editor, const gchar *what, gboolean match_case);

/* How it looks and behaves. */
void gtk_editor_set_font(GtkEditor *editor, GdkFont *font);
void gtk_editor_set_wrap(GtkEditor *editor, gboolean wrap);
void gtk_editor_set_line_numbers(GtkEditor *editor, gboolean numbers);
void gtk_editor_set_auto_indent(GtkEditor *editor, gboolean auto_indent);
void gtk_editor_set_tab_width(GtkEditor *editor, gint spaces);
gint gtk_editor_get_tab_width(GtkEditor *editor);

#endif /* GTK_EDITOR_H */
