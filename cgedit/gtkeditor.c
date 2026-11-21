/* gtkeditor.c - a widget that edits plain text

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

#include <config.h>

#include <stdio.h>
#include <string.h>

#include <gdk/gdkkeysyms.h>

#include "gtkeditor.h"

#ifdef ENABLE_NLS
#include <libintl.h>
#define _(String) gettext(String)
#else
#define _(String) (String)
#endif
#define N_(String) (String)

enum {
    PAD = 3,     /* bare space beside the text, and beside the line numbers */
    NEAR = 40,   /* how much comes into view with the cursor when it is brought in sideways */
    DRAG = 5,    /* how far the pointer goes before what is selected is being dragged */
    SCROLL = 50  /* milliseconds between lines, selecting past the top or the bottom */
};

enum { CHANGED, MOVED, FILES_DROPPED, LAST_SIGNAL };

static GtkWidgetClass *parent_class;
static guint signals[LAST_SIGNAL];
static GdkAtom clipboard, utf8_string;

static const GtkTargetEntry text_targets[] = {
    { "UTF8_STRING", 0, 0 },
    { "STRING", 0, 0 }
};

/* What can be dropped: files, by their names, before text. */
static const GtkTargetEntry drop_targets[] = {
    { "text/uri-list", 0, 1 },
    { "UTF8_STRING", 0, 0 },
    { "STRING", 0, 0 }
};

static void settle(GtkEditor *ed);

static GdkFont *
font_of(GtkEditor *ed)
{
    return ed->font ? ed->font : GTK_WIDGET(ed)->style->font;
}

/* How wide a character is in the font. Those of ASCII are known; the rest are looked up and kept. */
static gint
char_w(GtkEditor *ed, GdkWChar c)
{
    if (c < 128)
        return ed->ascii_w[c];
    if (ed->wide_w[c & 255].c != c) {
        ed->wide_w[c & 255].c = c;
        ed->wide_w[c & 255].w = gdk_char_width_wc(font_of(ed), c);
    }
    return ed->wide_w[c & 255].w;
}

/* How wide the line numbers are: room for three figures at the least. */
static gint
margin_for(GtkEditor *ed)
{
    gsize lines = ed->text->lines;
    gint figures = 1, i, w = 0;

    if (!ed->numbers)
        return 0;
    for (; lines >= 10; lines /= 10)
        figures++;
    for (i = '0'; i <= '9'; i++)
        w = MAX(w, ed->ascii_w[i]);
    return MAX(figures, 3) * w + 2 * PAD;
}

/*
 * A line is laid out: its bytes are made characters, each is given
 * its width and its place across, and where the line is wrapped a new
 * row starts, after the last space that fits or, in a word too long
 * for a row, wherever the row is full. A tab goes to the next of the
 * stops that stand every so many spaces.
 */
static GtkEditorLine *
layout(GtkEditor *ed, gsize line)
{
    GtkEditorLine *l = &ed->laid;
    gsize start = text_line_start(ed->text, line), end = text_line_end(ed->text, line), pos;
    gint tab = MAX(ed->tab_width * ed->space_w, 1), n = 0, x = 0, from = 0, space = -1, i, w;
    const guchar *s;

    if (ed->laid_stamp == ed->stamp && ed->laid_line == line && l->size)
        return l;
    ed->laid_stamp = ed->stamp;
    ed->laid_line = line;
    if ((gsize) l->size < end - start + 2) {
        l->size = (end - start + 2) * 3 / 2;
        l->ch = g_renew(GdkWChar, l->ch, l->size);
        l->at = g_renew(gsize, l->at, l->size);
        l->x = g_renew(gint, l->x, l->size);
        l->w = g_renew(gint, l->w, l->size);
    }
    s = (const guchar *) text_range(ed->text, start, end) - start;
    for (pos = start; pos < end; n++) {
        guchar c = s[pos];
        gint more = c < 0x80 ? 0 : c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1;

        l->at[n] = pos++;
        l->ch[n] = more ? c & (0x3f >> more) : c;
        while (more-- && pos < end)
            l->ch[n] = l->ch[n] << 6 | (s[pos++] & 0x3f);
    }
    l->at[n] = end;
    l->n = n;

    l->rows = 0;
    for (i = 0;; i++) {
        if (i == from || i == n) {
            if (l->rows + 2 > l->row_size) {
                l->row_size = l->rows * 2 + 8;
                l->row = g_renew(gint, l->row, l->row_size);
            }
            if (i == n) {
                if (!l->rows)
                    l->row[l->rows++] = 0;
                l->row[l->rows] = n;
                break;
            }
            l->row[l->rows++] = i;
        }
        w = l->ch[i] == '\t' ? tab - x % tab : l->ch[i] < ' ' ? ed->space_w : char_w(ed, l->ch[i]);
        if (ed->wrap && x + w > ed->wrap_w && i > from) {
            from = space >= from ? space + 1 : i;
            x = 0;
            space = -1;
            i = from - 1;
            continue;
        }
        l->x[i] = x;
        l->w[i] = w;
        x += w;
        if (l->ch[i] == ' ' || l->ch[i] == '\t')
            space = i;
    }
    return l;
}

static gint
line_width(const GtkEditorLine *l)
{
    return l->n ? l->x[l->n - 1] + l->w[l->n - 1] : 0;
}

static void
rows_room(GtkEditor *ed, gsize lines)
{
    if (lines + 1 > ed->rows_size) {
        ed->rows_size = (lines + 1) * 3 / 2 + 16;
        ed->rows = g_renew(guint, ed->rows, ed->rows_size);
        ed->first = g_renew(gsize, ed->first, ed->rows_size);
        ed->first[0] = 0;
    }
}

/* The row a line starts on, counted from the top of the text; for the line after the last, how many rows there are. */
static gsize
first_row(GtkEditor *ed, gsize line)
{
    if (!ed->wrap)
        return line;
    for (; ed->first_ok < line; ed->first_ok++)
        ed->first[ed->first_ok + 1] = ed->first[ed->first_ok] + ed->rows[ed->first_ok];
    return ed->first[line];
}

/* The line a row is part of, and which of the line's rows it is. */
static gsize
line_at_row(GtkEditor *ed, gsize row, gint *sub)
{
    gsize lo = 0, hi = ed->text->lines - 1, mid;

    *sub = 0;
    if (!ed->wrap)
        return MIN(row, hi);
    row = MIN(row, first_row(ed, hi + 1) - 1);
    while (lo < hi) {
        mid = (lo + hi + 1) / 2;
        if (ed->first[mid] <= row)
            lo = mid;
        else
            hi = mid - 1;
    }
    *sub = row - ed->first[lo];
    return lo;
}

/*
 * Everything is measured again: the font, the room for line numbers,
 * the width lines are wrapped at, and then every line, for how many
 * rows it takes or, where lines are not wrapped, for the widest.
 */
static void
relayout(GtkEditor *ed)
{
    GtkWidget *widget = GTK_WIDGET(ed);
    GdkFont *font = font_of(ed);
    gsize line;
    gint i;

    ed->stamp++;
    ed->line_h = MAX(font->ascent + font->descent, 1);
    ed->ascent = font->ascent;
    for (i = 0; i < 128; i++)
        ed->ascii_w[i] = i < ' ' || i == 127 ? 0 : gdk_char_width_wc(font, i);
    ed->space_w = MAX(ed->ascii_w[' '], 1);
    memset(ed->wide_w, 0, sizeof ed->wide_w);
    ed->margin = margin_for(ed);
    /* Before the widget has a size, a line is as long as it likes. */
    ed->wrap_w = widget->allocation.width <= 1 ? G_MAXINT / 2
        : MAX(widget->allocation.width - ed->margin - 2 * PAD, 8 * ed->space_w);
    ed->max_w = 0;
    if (ed->wrap) {
        rows_room(ed, ed->text->lines);
        for (line = 0; line < ed->text->lines; line++)
            ed->rows[line] = layout(ed, line)->rows;
        ed->first_ok = 0;
    } else
        for (line = 0; line < ed->text->lines; line++)
            ed->max_w = MAX(ed->max_w, line_width(layout(ed, line)));
}

/*
 * The text says it has changed: the lines there are laid out again,
 * and those after them start on other rows. The widest line only
 * grows, until everything is next measured.
 */
static void
text_changed(TextBuf *text, gsize pos, gsize gone, gsize come, gpointer data)
{
    GtkEditor *ed = data;
    gsize line = text_line_of(text, pos), was = text->lines + gone - come, i;

    ed->stamp++;
    ed->stale = TRUE;
    if (ed->wrap) {
        rows_room(ed, MAX(text->lines, was));
        memmove(ed->rows + line + 1 + come, ed->rows + line + 1 + gone,
                (was - line - 1 - gone) * sizeof *ed->rows);
        for (i = line; i <= line + come; i++)
            ed->rows[i] = layout(ed, i)->rows;
        ed->first_ok = MIN(ed->first_ok, line);
    } else
        for (i = line; i <= line + come; i++)
            ed->max_w = MAX(ed->max_w, line_width(layout(ed, i)));
}

static void
adjust(GtkAdjustment *adj, gint upper, gint page, gint step)
{
    gfloat value;

    if (!adj)
        return;
    value = CLAMP(adj->value, 0, MAX(upper - page, 0));
    adj->lower = 0;
    adj->upper = MAX(upper, page);
    adj->page_size = page;
    adj->page_increment = MAX(page - step, step);
    adj->step_increment = step;
    gtk_adjustment_changed(adj);
    if (value != adj->value)
        gtk_adjustment_set_value(adj, value);
}

/* The scroll bars are told how much there is: all the rows down, and the widest line across. */
static void
sized(GtkEditor *ed)
{
    GtkWidget *widget = GTK_WIDGET(ed);

    adjust(ed->vadj, first_row(ed, ed->text->lines) * ed->line_h, widget->allocation.height, ed->line_h);
    adjust(ed->hadj, ed->wrap ? 0 : ed->margin + 2 * PAD + ed->max_w + ed->space_w,
           widget->allocation.width, 4 * ed->space_w);
}

static gint
scroll_x(GtkEditor *ed)
{
    return ed->hadj && !ed->wrap ? (gint) ed->hadj->value : 0;
}

static gint
scroll_y(GtkEditor *ed)
{
    return ed->vadj ? (gint) ed->vadj->value : 0;
}

/* Which character of a line a place in the text is. */
static gint
index_in(const GtkEditorLine *l, gsize pos)
{
    gint lo = 0, hi = l->n, mid;

    while (lo < hi) {
        mid = (lo + hi) / 2;
        if (l->at[mid] < pos)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

/* Where a place in the text is: which row, and how far across it. */
static void
where(GtkEditor *ed, gsize pos, gsize *row, gint *x)
{
    gsize line = text_line_of(ed->text, pos);
    GtkEditorLine *l = layout(ed, line);
    gint i = index_in(l, pos), r;

    for (r = l->rows - 1; r > 0 && l->row[r] > i; r--)
        ;
    *row = first_row(ed, line) + r;
    *x = i < l->row[r + 1] ? l->x[i] : i > l->row[r] ? l->x[i - 1] + l->w[i - 1] : 0;
}

/*
 * The place in the text that is so far across a row: before a
 * character if nearer its left than its right. The end of a row that
 * the line goes on after is before its last character, as the place
 * after that is the start of the next row.
 */
static gsize
place(GtkEditor *ed, gint x, gsize row)
{
    gint sub, i, a, b;
    GtkEditorLine *l = layout(ed, line_at_row(ed, row, &sub));

    a = l->row[sub];
    b = l->row[sub + 1];
    for (i = a; i < b && x >= l->x[i] + (l->w[i] + 1) / 2; i++)
        ;
    if (i == b && sub + 1 < l->rows && b > a)
        i = b - 1;
    return l->at[i];
}

/* The place in the text under a point of the window. */
static gsize
place_at(GtkEditor *ed, gint wx, gint wy)
{
    gint y = wy + scroll_y(ed);

    return place(ed, wx - ed->margin - PAD + scroll_x(ed), y < 0 ? 0 : y / ed->line_h);
}

/* The cursor is brought into view, with a little of what is beside it. */
static void
show_cursor(GtkEditor *ed)
{
    GtkWidget *widget = GTK_WIDGET(ed);
    gint x, y, v = scroll_y(ed), h = scroll_x(ed), wide = widget->allocation.width - ed->margin - 2 * PAD;
    gsize row;

    where(ed, ed->cursor, &row, &x);
    y = row * ed->line_h;
    if (ed->vadj) {
        if (y < v)
            v = y;
        else if (y + ed->line_h > v + widget->allocation.height)
            v = y + ed->line_h - widget->allocation.height;
        v = CLAMP(v, 0, MAX((gint) (ed->vadj->upper - ed->vadj->page_size), 0));
        if (v != (gint) ed->vadj->value)
            gtk_adjustment_set_value(ed->vadj, v);
    }
    if (ed->hadj && !ed->wrap) {
        if (x < h)
            h = x - NEAR;
        else if (x > h + wide)
            h = x - wide + NEAR;
        h = CLAMP(h, 0, MAX((gint) (ed->hadj->upper - ed->hadj->page_size), 0));
        if (h != (gint) ed->hadj->value)
            gtk_adjustment_set_value(ed->hadj, h);
    }
}

static gboolean
selected(GtkEditor *ed, gsize *start, gsize *end)
{
    *start = MIN(ed->cursor, ed->anchor);
    *end = MAX(ed->cursor, ed->anchor);
    return *start < *end;
}

/*
 * Something has been done: whoever listens is told, the scroll bars
 * are put right, the cursor is brought into view and the widget is
 * drawn again. With something selected, the selection is this
 * widget's to give.
 */
static void
settle(GtkEditor *ed)
{
    GtkWidget *widget = GTK_WIDGET(ed);
    gsize start, end, row;
    GdkRectangle at;
    gint x;

    ed->cursor = MIN(ed->cursor, ed->text->len);
    ed->anchor = MIN(ed->anchor, ed->text->len);
    if (ed->stale) {
        ed->stale = FALSE;
        if (margin_for(ed) != ed->margin)
            relayout(ed);
        sized(ed);
        gtk_signal_emit(GTK_OBJECT(ed), signals[CHANGED]);
    }
    show_cursor(ed);
    if (GTK_WIDGET_REALIZED(widget)) {
        if (selected(ed, &start, &end))
            gtk_selection_owner_set(widget, GDK_SELECTION_PRIMARY, GDK_CURRENT_TIME);
        where(ed, ed->cursor, &row, &x);
        at.x = CLAMP(x + ed->margin + PAD - scroll_x(ed), 0, widget->allocation.width);
        at.y = CLAMP((glong) row * ed->line_h - scroll_y(ed), 0, widget->allocation.height);
        at.width = 1;
        at.height = ed->line_h;
        gtk_im_context_set_cursor_location(ed->im, &at);
    }
    gtk_signal_emit(GTK_OBJECT(ed), signals[MOVED]);
    gtk_widget_queue_draw(widget);
}

/* The cursor goes to a place, taking the selection's end with it or leaving it. */
static void
move_to(GtkEditor *ed, gsize pos, gboolean extend, gboolean keep_x)
{
    text_break(ed->text);
    ed->cursor = pos;
    if (!extend)
        ed->anchor = pos;
    if (!keep_x)
        ed->want_x = -1;
    settle(ed);
}

/* Up or down so many rows, keeping as far across as it was when it set out. */
static void
move_rows(GtkEditor *ed, gint by, gboolean extend)
{
    gsize row, rows = first_row(ed, ed->text->lines);
    gint x;

    where(ed, ed->cursor, &row, &x);
    if (ed->want_x < 0)
        ed->want_x = x;
    row = by < 0 ? ((gsize) -by > row ? 0 : row + by) : MIN(row + by, rows - 1);
    move_to(ed, place(ed, ed->want_x, row), extend, TRUE);
}

/* Where the row the cursor is on starts, and where it ends. */
static gsize
row_edge(GtkEditor *ed, gboolean end)
{
    GtkEditorLine *l = layout(ed, text_line_of(ed->text, ed->cursor));
    gint i = index_in(l, ed->cursor), r;

    for (r = l->rows - 1; r > 0 && l->row[r] > i; r--)
        ;
    if (!end)
        return l->at[l->row[r]];
    return l->at[r + 1 < l->rows && l->row[r + 1] > l->row[r] ? l->row[r + 1] - 1 : l->row[r + 1]];
}

static gboolean
in_word(gunichar c)
{
    return c == '_' || g_unichar_isalnum(c);
}

static gsize
word_start(GtkEditor *ed, gsize pos)
{
    TextBuf *t = ed->text;

    while (pos > 0 && !in_word(text_char(t, text_prev(t, pos))))
        pos = text_prev(t, pos);
    while (pos > 0 && in_word(text_char(t, text_prev(t, pos))))
        pos = text_prev(t, pos);
    return pos;
}

static gsize
word_end(GtkEditor *ed, gsize pos)
{
    TextBuf *t = ed->text;

    while (pos < t->len && !in_word(text_char(t, pos)))
        pos = text_next(t, pos);
    while (pos < t->len && in_word(text_char(t, pos)))
        pos = text_next(t, pos);
    return pos;
}

/* Part of the text becomes something else, and the cursor is after it. */
static void
replace(GtkEditor *ed, gsize start, gsize end, const gchar *s, gsize n)
{
    gboolean both = end > start && n;

    if (both)
        text_begin(ed->text);
    text_delete(ed->text, start, end - start);
    text_insert(ed->text, start, s, n);
    if (both)
        text_end(ed->text);
    ed->cursor = ed->anchor = start + n;
    ed->want_x = -1;
}

/*
 * Text comes in at the cursor, in place of what is selected; or, when
 * typing writes over, in place of the character there, unless that is
 * the end of the line.
 */
static void
put_in(GtkEditor *ed, const gchar *s, gsize n, gboolean typed)
{
    gsize start, end;

    if (!selected(ed, &start, &end) && typed && ed->overwrite
        && end < text_line_end(ed->text, text_line_of(ed->text, end)))
        end = text_next(ed->text, end);
    replace(ed, start, end, s, n);
    settle(ed);
}

/* What is shown up is shown up no longer: the user has gone on to something else. */
static void
unmark(GtkEditor *ed)
{
    g_free(ed->mark);
    ed->mark = NULL;
}

/*
 * The lines the selection touches, or the one the cursor is on, are
 * set a tab further in, or brought back out by a tab or as many
 * spaces as make one; and are then what is selected.
 */
static void
indent(GtkEditor *ed, gboolean out)
{
    TextBuf *t = ed->text;
    gsize start, end, first, last, line, pos, n;

    selected(ed, &start, &end);
    first = text_line_of(t, start);
    last = text_line_of(t, end);
    if (last > first && end == text_line_start(t, last))
        last--;
    text_begin(t);
    for (line = first; line <= last; line++) {
        pos = text_line_start(t, line);
        if (!out)
            text_insert(t, pos, "\t", 1);
        else if (text_char(t, pos) == '\t')
            text_delete(t, pos, 1);
        else {
            for (n = 0; n < (gsize) ed->tab_width && text_char(t, pos + n) == ' '; n++)
                ;
            text_delete(t, pos, n);
        }
    }
    text_end(t);
    ed->anchor = text_line_start(t, first);
    ed->cursor = last + 1 < t->lines ? text_line_start(t, last + 1) : t->len;
    ed->want_x = -1;
    settle(ed);
}

/* A new line, which starts as far in as the one it comes after if that is wanted. */
static void
new_line(GtkEditor *ed)
{
    TextBuf *t = ed->text;
    gsize start, end, pos, from;
    GString *s = g_string_new("\n");

    selected(ed, &start, &end);
    from = text_line_start(t, text_line_of(t, start));
    for (pos = from; ed->auto_indent && pos < start && (text_char(t, pos) == ' ' || text_char(t, pos) == '\t'); pos++)
        g_string_append_c(s, text_char(t, pos));
    replace(ed, start, end, s->str, s->len);
    g_string_free(s, TRUE);
    settle(ed);
}

/* What comes from elsewhere is made UTF-8 with plain line ends, to be freed. */
static gchar *
clean(const gchar *s, gssize len, gsize *n)
{
    gchar *text = text_valid(s, len, n), *p, *q;

    for (p = q = text; *p; p++)
        if (*p != '\r')
            *q++ = *p;
        else if (p[1] != '\n')
            *q++ = '\n';
    *q = '\0';
    *n = q - text;
    return text;
}

/* Latin-1, which is what a STRING is, as UTF-8; and back, with a question for what it has not. */
static gchar *
from_latin1(const guchar *s, gint len)
{
    GString *out = g_string_new(NULL);
    gchar *text;
    gint i;

    for (i = 0; i < len; i++)
        if (s[i] < 0x80)
            g_string_append_c(out, s[i]);
        else {
            g_string_append_c(out, 0xc0 | s[i] >> 6);
            g_string_append_c(out, 0x80 | (s[i] & 0x3f));
        }
    text = out->str;
    g_string_free(out, FALSE);
    return text;
}

static gchar *
to_latin1(const gchar *s)
{
    gchar *out = g_malloc(strlen(s) + 1), *q = out;

    for (; *s; s = g_utf8_next_char(s)) {
        gunichar c = g_utf8_get_char(s);

        *q++ = c < 0x100 ? (gchar) c : '?';
    }
    *q = '\0';
    return out;
}

/* The widget's own doings, which the program may ask for too. */

void
gtk_editor_copy(GtkEditor *ed)
{
    gsize start, end;

    g_return_if_fail(GTK_IS_EDITOR(ed));
    if (!selected(ed, &start, &end))
        return;
    g_free(ed->copied);
    ed->copied = text_copy(ed->text, start, end);
    gtk_selection_owner_set(GTK_WIDGET(ed), clipboard, GDK_CURRENT_TIME);
}

void
gtk_editor_delete_selection(GtkEditor *ed)
{
    gsize start, end;

    g_return_if_fail(GTK_IS_EDITOR(ed));
    if (selected(ed, &start, &end)) {
        replace(ed, start, end, NULL, 0);
        settle(ed);
    }
}

void
gtk_editor_cut(GtkEditor *ed)
{
    gtk_editor_copy(ed);
    gtk_editor_delete_selection(ed);
}

void
gtk_editor_paste(GtkEditor *ed)
{
    g_return_if_fail(GTK_IS_EDITOR(ed));
    gtk_selection_convert(GTK_WIDGET(ed), clipboard, utf8_string, GDK_CURRENT_TIME);
}

void
gtk_editor_select_all(GtkEditor *ed)
{
    g_return_if_fail(GTK_IS_EDITOR(ed));
    text_break(ed->text);
    ed->anchor = 0;
    ed->cursor = ed->text->len;
    ed->want_x = -1;
    /* All of it is selected where the view is: it is not taken to the end. */
    gtk_signal_emit(GTK_OBJECT(ed), signals[MOVED]);
    if (GTK_WIDGET_REALIZED(ed) && ed->text->len)
        gtk_selection_owner_set(GTK_WIDGET(ed), GDK_SELECTION_PRIMARY, GDK_CURRENT_TIME);
    gtk_widget_queue_draw(GTK_WIDGET(ed));
}

void
gtk_editor_undo(GtkEditor *ed)
{
    gsize cursor;

    g_return_if_fail(GTK_IS_EDITOR(ed));
    if (text_undo(ed->text, &cursor)) {
        ed->cursor = ed->anchor = cursor;
        ed->want_x = -1;
        settle(ed);
    }
}

void
gtk_editor_redo(GtkEditor *ed)
{
    gsize cursor;

    g_return_if_fail(GTK_IS_EDITOR(ed));
    if (text_redo(ed->text, &cursor)) {
        ed->cursor = ed->anchor = cursor;
        ed->want_x = -1;
        settle(ed);
    }
}

/* Drawing. */

static void
ink(GtkEditor *ed, const GdkColor *colour)
{
    gdk_gc_set_foreground(ed->gc, (GdkColor *) colour);
}

/* Whether what is shown up starts at a character of a line. */
static gboolean
marked(GtkEditor *ed, const GtkEditorLine *l, gint i)
{
    gint k;

    if (i + ed->mark_n > l->n)
        return FALSE;
    for (k = 0; k < ed->mark_n; k++)
        if ((ed->mark_case ? l->ch[i + k] : g_unichar_tolower(l->ch[i + k])) != ed->mark[k])
            return FALSE;
    return TRUE;
}

/*
 * One row of a line is drawn across the whole window: its background;
 * behind its text, what is shown up and then what is selected, which
 * goes a space past the end of a row whose end is selected too; and
 * its text, a run of characters at a time, tabs being only room.
 * Only what is in view sideways is looked at, so a very long line
 * costs no more to draw than a short one.
 */
static void
draw_row(GtkEditor *ed, const GtkEditorLine *l, gint sub, gint y)
{
    GtkWidget *widget = GTK_WIDGET(ed);
    GtkStyle *style = widget->style;
    gint a = l->row[sub], b = l->row[sub + 1], tx = ed->margin + PAD - scroll_x(ed), i, j, k, lo, hi, x0, x1;
    gint left = -tx, right = widget->allocation.width - tx;
    gsize s0, s1;
    gboolean sel = selected(ed, &s0, &s1);
    const GdkColor *base = &style->base[GTK_STATE_NORMAL], *chosen = &style->base[GTK_STATE_SELECTED];
    GdkColor mix;

    ink(ed, base);
    gdk_draw_rectangle(widget->window, ed->gc, TRUE, ed->margin, y, widget->allocation.width, ed->line_h);

    /* The first character in view and the one after the last, found by halving. */
    for (lo = a, hi = b; lo < hi;) {
        i = (lo + hi) / 2;
        if (l->x[i] + l->w[i] <= left)
            lo = i + 1;
        else
            hi = i;
    }
    a = lo;
    for (hi = b; lo < hi;) {
        i = (lo + hi) / 2;
        if (l->x[i] < right)
            lo = i + 1;
        else
            hi = i;
    }
    hi = lo;

    if (ed->mark) {
        mix.red = (base->red + chosen->red) / 2;
        mix.green = (base->green + chosen->green) / 2;
        mix.blue = (base->blue + chosen->blue) / 2;
        gdk_rgb_gc_set_foreground(ed->gc, (mix.red >> 8) << 16 | (mix.green >> 8) << 8 | mix.blue >> 8);
        for (i = MAX(a - ed->mark_n + 1, 0); i < hi; i++)
            if (marked(ed, l, i)) {
                j = MAX(i, l->row[sub]);
                k = MIN(i + ed->mark_n, b);
                if (j < k)
                    gdk_draw_rectangle(widget->window, ed->gc, TRUE, tx + l->x[j], y,
                                       l->x[k - 1] + l->w[k - 1] - l->x[j], ed->line_h);
                i += ed->mark_n - 1;
            }
    }
    if (sel && s0 <= l->at[b] && s1 > l->at[l->row[sub]]) {
        for (i = l->row[sub]; i < b && l->at[i] < s0; i++)
            ;
        for (j = i; j < b && l->at[j] < s1; j++)
            ;
        x0 = i < b ? l->x[i] : b > l->row[sub] ? l->x[b - 1] + l->w[b - 1] : 0;
        x1 = j > i ? l->x[j - 1] + l->w[j - 1] : x0;
        if (j == b && s1 > l->at[b])
            x1 += ed->space_w;
        if (x1 > x0) {
            ink(ed, chosen);
            gdk_draw_rectangle(widget->window, ed->gc, TRUE, tx + x0, y, x1 - x0, ed->line_h);
        }
    }
    for (i = a; i < hi; i = j) {
        gboolean in = sel && l->at[i] >= s0 && l->at[i] < s1;

        if (l->ch[i] < ' ') {
            j = i + 1;
            continue;
        }
        for (j = i + 1; j < hi && l->ch[j] >= ' ' && (sel && l->at[j] >= s0 && l->at[j] < s1) == in; j++)
            ;
        ink(ed, &style->text[in ? GTK_STATE_SELECTED : GTK_STATE_NORMAL]);
        gdk_draw_text_wc(widget->window, font_of(ed), ed->gc, tx + l->x[i], y + ed->ascent, l->ch + i, j - i);
    }
}

/* What an input method is in the middle of is drawn over the text at the cursor, underlined. */
static void
draw_preedit(GtkEditor *ed, gint x, gint y)
{
    GtkWidget *widget = GTK_WIDGET(ed);
    glong n = g_utf8_strlen(ed->preedit, -1), i;
    GdkWChar *wc = g_new(GdkWChar, n + 1);
    const gchar *p = ed->preedit;
    gint w;

    for (i = 0; i < n; i++, p = g_utf8_next_char(p))
        wc[i] = g_utf8_get_char(p);
    w = gdk_text_width_wc(font_of(ed), wc, n);
    ink(ed, &widget->style->base[GTK_STATE_NORMAL]);
    gdk_draw_rectangle(widget->window, ed->gc, TRUE, x, y, w, ed->line_h);
    ink(ed, &widget->style->text[GTK_STATE_NORMAL]);
    gdk_draw_text_wc(widget->window, font_of(ed), ed->gc, x, y + ed->ascent, wc, n);
    gdk_draw_line(widget->window, ed->gc, x, y + ed->line_h - 1, x + w - 1, y + ed->line_h - 1);
    g_free(wc);
}

/*
 * The rows the damage touches are drawn whole, then the cursor if the
 * widget has the keyboard, and last the line numbers, each on the
 * first row of its line, over whatever text has scrolled under them.
 */
static gint
gtk_editor_expose(GtkWidget *widget, GdkEventExpose *event)
{
    GtkEditor *ed = GTK_EDITOR(widget);
    GtkStyle *style = widget->style;
    gint v = scroll_y(ed), sub, y, cx, top = MAX(event->area.y, 0);
    gint bottom = MIN(event->area.y + event->area.height, widget->allocation.height);
    gsize row = (top + v) / ed->line_h, last = (bottom - 1 + v) / ed->line_h, rows, line, crow, first;
    gchar figures[24];

    if (bottom <= top)
        return TRUE;
    if (!ed->gc)
        ed->gc = gdk_gc_new(widget->window);
    rows = first_row(ed, ed->text->lines);
    where(ed, ed->cursor, &crow, &cx);
    first = line = line_at_row(ed, row, &sub);
    for (; row <= last && row < rows; line++, sub = 0) {
        GtkEditorLine *l = layout(ed, line);

        for (; sub < l->rows && row <= last; sub++, row++)
            draw_row(ed, l, sub, row * ed->line_h - v);
    }
    y = rows * ed->line_h - v;
    if (y < bottom) {
        ink(ed, &style->base[GTK_STATE_NORMAL]);
        gdk_draw_rectangle(widget->window, ed->gc, TRUE, ed->margin, MAX(y, top),
                           widget->allocation.width, bottom - MAX(y, top));
    }

    cx += ed->margin + PAD - scroll_x(ed);
    y = crow * ed->line_h - v;
    if (GTK_WIDGET_HAS_FOCUS(widget) && y + ed->line_h > top && y < bottom) {
        ink(ed, &style->text[GTK_STATE_NORMAL]);
        if (ed->preedit && *ed->preedit)
            draw_preedit(ed, cx, y);
        else if (ed->overwrite)
            gdk_draw_rectangle(widget->window, ed->gc, FALSE, cx, y, MAX(ed->space_w - 1, 1), ed->line_h - 1);
        else
            gdk_draw_line(widget->window, ed->gc, cx, y, cx, y + ed->line_h - 1);
    }

    if (ed->margin) {
        ink(ed, &style->bg[GTK_STATE_NORMAL]);
        gdk_draw_rectangle(widget->window, ed->gc, TRUE, 0, top, ed->margin, bottom - top);
        for (line = first; line < ed->text->lines; line++) {
            y = first_row(ed, line) * ed->line_h - v;
            if (y >= bottom)
                break;
            g_snprintf(figures, sizeof figures, "%lu", (gulong) line + 1);
            gdk_draw_string(widget->window, font_of(ed), style->fg_gc[GTK_STATE_NORMAL],
                            ed->margin - PAD - gdk_string_width(font_of(ed), figures), y + ed->ascent, figures);
        }
    }
    return TRUE;
}

/* The keyboard. */

/*
 * A key the widget has a use for; the rest are left for the window's
 * menus. An input method sees every key first, and what is typed
 * comes from it. With Shift a key that moves the cursor selects.
 */
static gint
gtk_editor_key_press(GtkWidget *widget, GdkEventKey *event)
{
    GtkEditor *ed = GTK_EDITOR(widget);
    TextBuf *t = ed->text;
    gboolean shift = event->state & GDK_SHIFT_MASK, control = event->state & GDK_CONTROL_MASK;
    gint page = MAX(widget->allocation.height / ed->line_h - 1, 1);
    gsize start, end, pos = ed->cursor;
    gboolean sel = selected(ed, &start, &end);

    if (gtk_im_context_filter_keypress(ed->im, event))
        return TRUE;
    if (event->state & GDK_MOD1_MASK)
        return FALSE;

    switch (event->keyval) {
    case GDK_Left:
    case GDK_KP_Left:
        pos = sel && !shift && !control ? start : control ? word_start(ed, pos) : text_prev(t, pos);
        break;
    case GDK_Right:
    case GDK_KP_Right:
        pos = sel && !shift && !control ? end : control ? word_end(ed, pos) : text_next(t, pos);
        break;
    case GDK_Up:
    case GDK_KP_Up:
        unmark(ed);
        move_rows(ed, -1, shift);
        return TRUE;
    case GDK_Down:
    case GDK_KP_Down:
        unmark(ed);
        move_rows(ed, 1, shift);
        return TRUE;
    case GDK_Page_Up:
    case GDK_KP_Page_Up:
        unmark(ed);
        move_rows(ed, -page, shift);
        return TRUE;
    case GDK_Page_Down:
    case GDK_KP_Page_Down:
        unmark(ed);
        move_rows(ed, page, shift);
        return TRUE;
    case GDK_Home:
    case GDK_KP_Home:
        pos = control ? 0 : row_edge(ed, FALSE);
        break;
    case GDK_End:
    case GDK_KP_End:
        pos = control ? t->len : row_edge(ed, TRUE);
        break;
    case GDK_BackSpace:
        unmark(ed);
        if (!sel)
            start = control ? word_start(ed, pos) : text_prev(t, pos);
        replace(ed, start, end, NULL, 0);
        settle(ed);
        return TRUE;
    case GDK_Delete:
    case GDK_KP_Delete:
        unmark(ed);
        if (shift && sel)
            gtk_editor_copy(ed);
        if (!sel)
            end = control ? word_end(ed, pos) : text_next(t, pos);
        replace(ed, start, end, NULL, 0);
        settle(ed);
        return TRUE;
    case GDK_Return:
    case GDK_KP_Enter:
        if (control)
            return FALSE;
        unmark(ed);
        new_line(ed);
        return TRUE;
    case GDK_Tab:
    case GDK_ISO_Left_Tab:
        if (control)
            return FALSE;
        unmark(ed);
        if (shift || event->keyval == GDK_ISO_Left_Tab)
            indent(ed, TRUE);
        else if (sel && text_line_of(t, start) != text_line_of(t, end))
            indent(ed, FALSE);
        else
            put_in(ed, "\t", 1, FALSE);
        return TRUE;
    case GDK_Insert:
    case GDK_KP_Insert:
        if (shift)
            gtk_editor_paste(ed);
        else if (control)
            gtk_editor_copy(ed);
        else {
            ed->overwrite = !ed->overwrite;
            gtk_widget_queue_draw(widget);
        }
        return TRUE;
    case GDK_Menu:
        gtk_signal_emit_by_name(GTK_OBJECT(widget), "popup_menu");
        return TRUE;
    default:
        if (!control)
            return FALSE;
        switch (gdk_keyval_to_lower(event->keyval)) {
        case GDK_a: gtk_editor_select_all(ed); break;
        case GDK_c: gtk_editor_copy(ed); break;
        case GDK_x: gtk_editor_cut(ed); break;
        case GDK_v: gtk_editor_paste(ed); break;
        case GDK_y: gtk_editor_redo(ed); break;
        case GDK_z:
            if (shift)
                gtk_editor_redo(ed);
            else
                gtk_editor_undo(ed);
            break;
        default:
            return FALSE;
        }
        return TRUE;
    }
    unmark(ed);
    move_to(ed, pos, shift, FALSE);
    return TRUE;
}

static gint
gtk_editor_key_release(GtkWidget *widget, GdkEventKey *event)
{
    return gtk_im_context_filter_keypress(GTK_EDITOR(widget)->im, event);
}

static void
committed(GtkIMContext *im G_GNUC_UNUSED, const gchar *utf8, GtkEditor *ed)
{
    unmark(ed);
    put_in(ed, utf8, strlen(utf8), TRUE);
}

static void
preedit_changed(GtkIMContext *im, GtkEditor *ed)
{
    g_free(ed->preedit);
    gtk_im_context_get_preedit_string(im, &ed->preedit, NULL, NULL, NULL);
    gtk_widget_queue_draw(GTK_WIDGET(ed));
}

/* The pointer. */

static void
menu_did(GtkWidget *item, GtkEditor *ed)
{
    switch (GPOINTER_TO_INT(gtk_object_get_user_data(GTK_OBJECT(item)))) {
    case 0: gtk_editor_undo(ed); break;
    case 1: gtk_editor_redo(ed); break;
    case 2: gtk_editor_cut(ed); break;
    case 3: gtk_editor_copy(ed); break;
    case 4: gtk_editor_paste(ed); break;
    case 5: gtk_editor_delete_selection(ed); break;
    default: gtk_editor_select_all(ed); break;
    }
}

/* The menu of the right button: made once, and each time only what can be done can be chosen. */
static void
popup(GtkEditor *ed, guint button, guint32 time)
{
    static const gchar *const labels[] = {
        N_("Undo"), N_("Redo"), NULL, N_("Cut"), N_("Copy"), N_("Paste"), N_("Delete"), NULL, N_("Select All")
    };
    gsize start, end;
    gboolean sel = selected(ed, &start, &end);
    GList *l;
    guint i, n;

    if (!ed->menu) {
        ed->menu = gtk_menu_new();
        for (i = n = 0; i < G_N_ELEMENTS(labels); i++) {
            GtkWidget *item = labels[i] ? gtk_menu_item_new_with_label(_(labels[i])) : gtk_menu_item_new();

            if (labels[i]) {
                gtk_object_set_user_data(GTK_OBJECT(item), GINT_TO_POINTER(n++));
                gtk_signal_connect(GTK_OBJECT(item), "activate", GTK_SIGNAL_FUNC(menu_did), ed);
            } else
                gtk_widget_set_sensitive(item, FALSE);
            gtk_menu_append(GTK_MENU(ed->menu), item);
            gtk_widget_show(item);
        }
    }
    for (l = GTK_MENU_SHELL(ed->menu)->children, i = 0; l; l = l->next, i++)
        if (labels[i])
            gtk_widget_set_sensitive(l->data, i == 0 ? text_can_undo(ed->text) : i == 1 ? text_can_redo(ed->text)
                                     : i == 3 || i == 4 || i == 6 ? sel : TRUE);
    gtk_menu_popup(GTK_MENU(ed->menu), NULL, NULL, NULL, NULL, button, time);
}

/*
 * The left button puts the cursor where it is pressed, selects from
 * where the cursor was with Shift, a word pressed twice and a line
 * pressed three times; pressed on what is selected it waits, as that
 * may be dragged away. The middle one pastes what is selected
 * elsewhere, and the right one has a menu.
 */
static gint
gtk_editor_button_press(GtkWidget *widget, GdkEventButton *event)
{
    GtkEditor *ed = GTK_EDITOR(widget);
    TextBuf *t = ed->text;
    gsize pos, start, end, line;

    if (event->button > 3)
        return FALSE;
    if (!GTK_WIDGET_HAS_FOCUS(widget))
        gtk_widget_grab_focus(widget);
    gtk_im_context_reset(ed->im);
    pos = place_at(ed, event->x, event->y);
    if (event->button == 3) {
        if (event->type == GDK_BUTTON_PRESS)
            popup(ed, event->button, event->time);
        return TRUE;
    }
    unmark(ed);
    if (event->button == 2) {
        if (event->type != GDK_BUTTON_PRESS)
            return TRUE;
        /* What is selected here goes in here without being asked for: once the cursor moves it is selected no more. */
        if (selected(ed, &start, &end) && gdk_selection_owner_get(GDK_SELECTION_PRIMARY) == widget->window) {
            gchar *own = text_copy(t, start, end);

            move_to(ed, pos, FALSE, FALSE);
            put_in(ed, own, end - start, FALSE);
            g_free(own);
        } else {
            move_to(ed, pos, FALSE, FALSE);
            gtk_selection_convert(widget, GDK_SELECTION_PRIMARY, utf8_string, event->time);
        }
        return TRUE;
    }
    if (event->type == GDK_2BUTTON_PRESS) {
        ed->anchor = in_word(text_char(t, pos)) || (pos > 0 && in_word(text_char(t, text_prev(t, pos))))
            ? word_start(ed, in_word(text_char(t, pos)) ? text_next(t, pos) : pos) : pos;
        move_to(ed, in_word(text_char(t, ed->anchor)) ? word_end(ed, ed->anchor) : text_next(t, pos), TRUE, FALSE);
        ed->may_drag = FALSE;
    } else if (event->type == GDK_3BUTTON_PRESS) {
        line = text_line_of(t, pos);
        ed->anchor = text_line_start(t, line);
        move_to(ed, line + 1 < t->lines ? text_line_start(t, line + 1) : t->len, TRUE, FALSE);
        ed->may_drag = FALSE;
    } else if (selected(ed, &start, &end) && pos >= start && pos < end && !(event->state & GDK_SHIFT_MASK)) {
        ed->may_drag = TRUE;
        ed->press_x = event->x;
        ed->press_y = event->y;
    } else {
        ed->selecting = TRUE;
        move_to(ed, pos, event->state & GDK_SHIFT_MASK, FALSE);
    }
    return TRUE;
}

/* While the pointer is held past the top or the bottom, the text goes by a line at a time. */
static gint
scroll_on(gpointer data)
{
    GtkEditor *ed = data;
    GtkWidget *widget = GTK_WIDGET(ed);
    gint x, y;

    gdk_window_get_pointer(widget->window, &x, &y, NULL);
    if (!ed->selecting || (y >= 0 && y < widget->allocation.height)) {
        ed->scroller = 0;
        return FALSE;
    }
    move_to(ed, place_at(ed, x, y < 0 ? -ed->line_h : widget->allocation.height + ed->line_h - 1), TRUE, FALSE);
    return TRUE;
}

static gint
gtk_editor_motion(GtkWidget *widget, GdkEventMotion *event)
{
    GtkEditor *ed = GTK_EDITOR(widget);
    gint x, y;

    if (event->is_hint)
        gdk_window_get_pointer(widget->window, &x, &y, NULL);
    else {
        x = event->x;
        y = event->y;
    }
    if (ed->may_drag && (ABS(x - ed->press_x) > DRAG || ABS(y - ed->press_y) > DRAG)) {
        GtkTargetList *targets = gtk_target_list_new(text_targets, G_N_ELEMENTS(text_targets));

        ed->may_drag = FALSE;
        ed->dragging = TRUE;
        gtk_drag_begin(widget, targets, GDK_ACTION_COPY | GDK_ACTION_MOVE, 1, (GdkEvent *) event);
        gtk_target_list_unref(targets);
    } else if (ed->selecting) {
        gsize pos = place_at(ed, x, CLAMP(y, 0, widget->allocation.height - 1));

        if (pos != ed->cursor)
            move_to(ed, pos, TRUE, FALSE);
        if ((y < 0 || y >= widget->allocation.height) && !ed->scroller)
            ed->scroller = gtk_timeout_add(SCROLL, scroll_on, ed);
    }
    return TRUE;
}

static gint
gtk_editor_button_release(GtkWidget *widget, GdkEventButton *event)
{
    GtkEditor *ed = GTK_EDITOR(widget);

    if (event->button != 1)
        return FALSE;
    if (ed->may_drag)  /* pressed on what is selected and let go: the cursor goes there after all */
        move_to(ed, place_at(ed, event->x, event->y), FALSE, FALSE);
    ed->may_drag = ed->selecting = FALSE;
    return TRUE;
}

/* Giving and taking text: the selection, the clipboard, and what is dragged. */

static void
give(GtkEditor *ed, GtkSelectionData *data, const gchar *text)
{
    gchar *latin;

    if (data->target == utf8_string)
        gtk_selection_data_set(data, utf8_string, 8, (const guchar *) text, strlen(text));
    else {
        latin = to_latin1(text);
        gtk_selection_data_set(data, GDK_TARGET_STRING, 8, (const guchar *) latin, strlen(latin));
        g_free(latin);
    }
    (void) ed;
}

static void
gtk_editor_selection_get(GtkWidget *widget, GtkSelectionData *data, guint info G_GNUC_UNUSED, guint time G_GNUC_UNUSED)
{
    GtkEditor *ed = GTK_EDITOR(widget);
    gsize start, end;
    gchar *text;

    if (data->selection == clipboard) {
        if (ed->copied)
            give(ed, data, ed->copied);
    } else if (selected(ed, &start, &end)) {
        text = text_copy(ed->text, start, end);
        give(ed, data, text);
        g_free(text);
    }
}

/* Text has come, as UTF-8 or as a STRING; if UTF-8 was refused, a STRING is asked for. */
static gchar *
taken(GtkSelectionData *data, gsize *n)
{
    gchar *latin, *text;

    if (data->length <= 0 || data->format != 8)
        return NULL;
    if (data->type == GDK_TARGET_STRING) {
        latin = from_latin1(data->data, data->length);
        text = clean(latin, -1, n);
        g_free(latin);
        return text;
    }
    return clean((const gchar *) data->data, data->length, n);
}

static void
gtk_editor_selection_received(GtkWidget *widget, GtkSelectionData *data, guint time)
{
    GtkEditor *ed = GTK_EDITOR(widget);
    gchar *text;
    gsize n;

    if (data->length < 0 && data->target == utf8_string)
        gtk_selection_convert(widget, data->selection, GDK_TARGET_STRING, time);
    else if ((text = taken(data, &n))) {
        unmark(ed);
        put_in(ed, text, n, FALSE);
        g_free(text);
    }
}

static gint
gtk_editor_selection_clear(GtkWidget *widget, GdkEventSelection *event)
{
    GtkEditor *ed = GTK_EDITOR(widget);

    if (!gtk_selection_clear(widget, event))
        return FALSE;
    if (event->selection == clipboard) {
        g_free(ed->copied);
        ed->copied = NULL;
    }
    return TRUE;
}

static void
gtk_editor_drag_data_get(GtkWidget *widget, GdkDragContext *context G_GNUC_UNUSED, GtkSelectionData *data,
                         guint info G_GNUC_UNUSED, guint time G_GNUC_UNUSED)
{
    GtkEditor *ed = GTK_EDITOR(widget);
    gsize start, end;
    gchar *text;

    if (selected(ed, &start, &end)) {
        text = text_copy(ed->text, start, end);
        give(ed, data, text);
        g_free(text);
    }
}

/* What was dragged away was moved, not copied: it goes from here. */
static void
gtk_editor_drag_data_delete(GtkWidget *widget, GdkDragContext *context G_GNUC_UNUSED)
{
    gtk_editor_delete_selection(GTK_EDITOR(widget));
}

static void
gtk_editor_drag_end(GtkWidget *widget, GdkDragContext *context G_GNUC_UNUSED)
{
    GTK_EDITOR(widget)->dragging = FALSE;
}

/*
 * Text is dropped: it goes in where it was dropped and is selected.
 * Dropped from this same widget it is moved there, in one step, or
 * copied if Control is held; dropped on itself it stays where it is.
 * Files dropped are not the text's business: whoever has the widget
 * is told their names, as the list of URIs they came as.
 */
static void
gtk_editor_drag_data_received(GtkWidget *widget, GdkDragContext *context, gint x, gint y,
                              GtkSelectionData *data, guint info, guint time)
{
    GtkEditor *ed = GTK_EDITOR(widget);
    gboolean own = gtk_drag_get_source_widget(context) == widget, move;
    gsize pos = place_at(ed, x, y), start, end, n;
    GdkModifierType held;
    gchar *text;

    if (info == 1) {
        text = data->length > 0 && data->format == 8 ? g_strndup((const gchar *) data->data, data->length) : NULL;
        gtk_drag_finish(context, text != NULL, FALSE, time);
        if (text)
            gtk_signal_emit(GTK_OBJECT(ed), signals[FILES_DROPPED], text);
        g_free(text);
        return;
    }
    /* Its own text is moved unless Control is held, whatever the drag would rather. */
    gdk_window_get_pointer(widget->window, NULL, NULL, &held);
    move = own && !(held & GDK_CONTROL_MASK);
    text = taken(data, &n);
    selected(ed, &start, &end);
    if (!text || (own && pos >= start && pos <= end)) {
        gtk_drag_finish(context, FALSE, FALSE, time);
        g_free(text);
        return;
    }
    unmark(ed);
    text_begin(ed->text);
    if (move) {
        text_delete(ed->text, start, end - start);
        if (pos > end)
            pos -= end - start;
    }
    text_insert(ed->text, pos, text, n);
    text_end(ed->text);
    ed->anchor = pos;
    ed->cursor = pos + n;
    ed->want_x = -1;
    settle(ed);
    g_free(text);
    /* Its own move is done already; anyone else's is theirs to finish. */
    gtk_drag_finish(context, TRUE, !own && context->action == GDK_ACTION_MOVE, time);
}

/* Being a widget. */

static void
scrolled(GtkAdjustment *adj G_GNUC_UNUSED, GtkEditor *ed)
{
    gtk_widget_queue_draw(GTK_WIDGET(ed));
}

static void
take_adjustment(GtkEditor *ed, GtkAdjustment **mine, GtkAdjustment *adj)
{
    if (*mine == adj)
        return;
    if (*mine) {
        gtk_signal_disconnect_by_data(GTK_OBJECT(*mine), ed);
        gtk_object_unref(GTK_OBJECT(*mine));
    }
    if ((*mine = adj)) {
        gtk_object_ref(GTK_OBJECT(adj));
        gtk_object_sink(GTK_OBJECT(adj));
        gtk_signal_connect(GTK_OBJECT(adj), "value_changed", GTK_SIGNAL_FUNC(scrolled), ed);
    }
}

static void
gtk_editor_set_scroll_adjustments(GtkEditor *ed, GtkAdjustment *hadj, GtkAdjustment *vadj)
{
    take_adjustment(ed, &ed->hadj, hadj);
    take_adjustment(ed, &ed->vadj, vadj);
    sized(ed);
}

static void
gtk_editor_realize(GtkWidget *widget)
{
    GtkEditor *ed = GTK_EDITOR(widget);
    GdkWindowAttr attributes;
    GdkCursor *cursor;

    GTK_WIDGET_SET_FLAGS(widget, GTK_REALIZED);
    attributes.window_type = GDK_WINDOW_CHILD;
    attributes.x = widget->allocation.x;
    attributes.y = widget->allocation.y;
    attributes.width = widget->allocation.width;
    attributes.height = widget->allocation.height;
    attributes.wclass = GDK_INPUT_OUTPUT;
    attributes.visual = gtk_widget_get_visual(widget);
    attributes.colormap = gtk_widget_get_colormap(widget);
    attributes.event_mask = gtk_widget_get_events(widget) | GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK
        | GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK
        | GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK;
    widget->window = gdk_window_new(gtk_widget_get_parent_window(widget), &attributes,
                                    GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP);
    gdk_window_set_user_data(widget->window, widget);
    widget->style = gtk_style_attach(widget->style, widget->window);
    gdk_window_set_background(widget->window, &widget->style->base[GTK_STATE_NORMAL]);
    cursor = gdk_cursor_new(GDK_XTERM);
    gdk_window_set_cursor(widget->window, cursor);
    gdk_cursor_destroy(cursor);
    gtk_im_context_set_client_window(ed->im, widget->window);
}

static void
gtk_editor_unrealize(GtkWidget *widget)
{
    GtkEditor *ed = GTK_EDITOR(widget);

    gtk_im_context_set_client_window(ed->im, NULL);
    if (ed->gc)
        gdk_gc_unref(ed->gc);
    ed->gc = NULL;
    if (parent_class->unrealize)
        parent_class->unrealize(widget);
}

static void
gtk_editor_size_request(GtkWidget *widget G_GNUC_UNUSED, GtkRequisition *requisition)
{
    requisition->width = 120;
    requisition->height = 60;
}

/* A new width wraps the lines anew, which is all of them measured again. */
static void
gtk_editor_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
    GtkEditor *ed = GTK_EDITOR(widget);
    gboolean wider = allocation->width != widget->allocation.width;

    widget->allocation = *allocation;
    if (GTK_WIDGET_REALIZED(widget))
        gdk_window_move_resize(widget->window, allocation->x, allocation->y, allocation->width, allocation->height);
    if (wider && ed->wrap)
        relayout(ed);
    sized(ed);
}

static void
gtk_editor_style_set(GtkWidget *widget, GtkStyle *was G_GNUC_UNUSED)
{
    GtkEditor *ed = GTK_EDITOR(widget);

    if (GTK_WIDGET_REALIZED(widget))
        gdk_window_set_background(widget->window, &widget->style->base[GTK_STATE_NORMAL]);
    relayout(ed);
    sized(ed);
}

static gint
gtk_editor_focus_in(GtkWidget *widget, GdkEventFocus *event G_GNUC_UNUSED)
{
    GTK_WIDGET_SET_FLAGS(widget, GTK_HAS_FOCUS);
    gtk_im_context_focus_in(GTK_EDITOR(widget)->im);
    gtk_widget_queue_draw(widget);
    return FALSE;
}

static gint
gtk_editor_focus_out(GtkWidget *widget, GdkEventFocus *event G_GNUC_UNUSED)
{
    GTK_WIDGET_UNSET_FLAGS(widget, GTK_HAS_FOCUS);
    gtk_im_context_focus_out(GTK_EDITOR(widget)->im);
    gtk_widget_queue_draw(widget);
    return FALSE;
}

static void
gtk_editor_destroy(GtkObject *object)
{
    GtkEditor *ed = GTK_EDITOR(object);

    if (ed->scroller)
        gtk_timeout_remove(ed->scroller);
    ed->scroller = 0;
    take_adjustment(ed, &ed->hadj, NULL);
    take_adjustment(ed, &ed->vadj, NULL);
    if (ed->menu)
        gtk_widget_destroy(ed->menu);
    ed->menu = NULL;
    if (ed->im) {
        gtk_signal_disconnect_by_data(GTK_OBJECT(ed->im), ed);
        gtk_object_unref(GTK_OBJECT(ed->im));
    }
    ed->im = NULL;
    if (GTK_OBJECT_CLASS(parent_class)->destroy)
        GTK_OBJECT_CLASS(parent_class)->destroy(object);
}

static void
gtk_editor_finalize(GtkObject *object)
{
    GtkEditor *ed = GTK_EDITOR(object);

    text_free(ed->text);
    if (ed->font)
        gdk_font_unref(ed->font);
    g_free(ed->rows);
    g_free(ed->first);
    g_free(ed->laid.ch);
    g_free(ed->laid.at);
    g_free(ed->laid.x);
    g_free(ed->laid.w);
    g_free(ed->laid.row);
    g_free(ed->mark);
    g_free(ed->copied);
    g_free(ed->preedit);
    GTK_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
gtk_editor_class_init(GtkEditorClass *klass)
{
    GtkObjectClass *object_class = (GtkObjectClass *) klass;
    GtkWidgetClass *widget_class = (GtkWidgetClass *) klass;

    parent_class = gtk_type_class(gtk_widget_get_type());
    clipboard = gdk_atom_intern("CLIPBOARD", FALSE);
    utf8_string = gdk_atom_intern("UTF8_STRING", FALSE);

    signals[CHANGED] = gtk_signal_new("changed", GTK_RUN_FIRST, object_class->type,
                                      GTK_SIGNAL_OFFSET(GtkEditorClass, changed),
                                      gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);
    signals[MOVED] = gtk_signal_new("moved", GTK_RUN_FIRST, object_class->type,
                                    GTK_SIGNAL_OFFSET(GtkEditorClass, moved),
                                    gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);
    signals[FILES_DROPPED] = gtk_signal_new("files_dropped", GTK_RUN_FIRST, object_class->type,
                                            GTK_SIGNAL_OFFSET(GtkEditorClass, files_dropped),
                                            gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1, GTK_TYPE_POINTER);
    gtk_object_class_add_signals(object_class, signals, LAST_SIGNAL);
    widget_class->set_scroll_adjustments_signal =
        gtk_signal_new("set_scroll_adjustments", GTK_RUN_LAST, object_class->type,
                       GTK_SIGNAL_OFFSET(GtkEditorClass, set_scroll_adjustments),
                       gtk_marshal_NONE__POINTER_POINTER,
                       GTK_TYPE_NONE, 2, GTK_TYPE_ADJUSTMENT, GTK_TYPE_ADJUSTMENT);

    object_class->destroy = gtk_editor_destroy;
    object_class->finalize = gtk_editor_finalize;
    widget_class->realize = gtk_editor_realize;
    widget_class->unrealize = gtk_editor_unrealize;
    widget_class->size_request = gtk_editor_size_request;
    widget_class->size_allocate = gtk_editor_size_allocate;
    widget_class->style_set = gtk_editor_style_set;
    widget_class->expose_event = gtk_editor_expose;
    widget_class->key_press_event = gtk_editor_key_press;
    widget_class->key_release_event = gtk_editor_key_release;
    widget_class->button_press_event = gtk_editor_button_press;
    widget_class->button_release_event = gtk_editor_button_release;
    widget_class->motion_notify_event = gtk_editor_motion;
    widget_class->focus_in_event = gtk_editor_focus_in;
    widget_class->focus_out_event = gtk_editor_focus_out;
    widget_class->selection_get = gtk_editor_selection_get;
    widget_class->selection_received = gtk_editor_selection_received;
    widget_class->selection_clear_event = gtk_editor_selection_clear;
    widget_class->drag_data_get = gtk_editor_drag_data_get;
    widget_class->drag_data_delete = gtk_editor_drag_data_delete;
    widget_class->drag_data_received = gtk_editor_drag_data_received;
    widget_class->drag_end = gtk_editor_drag_end;
    klass->set_scroll_adjustments = gtk_editor_set_scroll_adjustments;
}

static void
gtk_editor_init(GtkEditor *ed, GtkEditorClass *klass G_GNUC_UNUSED)
{
    GtkWidget *widget = GTK_WIDGET(ed);

    GTK_WIDGET_SET_FLAGS(widget, GTK_CAN_FOCUS);
    ed->tab_width = 8;
    ed->want_x = -1;
    ed->text = text_new(text_changed, ed);
    ed->im = gtk_im_multicontext_new();
    gtk_signal_connect(GTK_OBJECT(ed->im), "commit", GTK_SIGNAL_FUNC(committed), ed);
    gtk_signal_connect(GTK_OBJECT(ed->im), "preedit_changed", GTK_SIGNAL_FUNC(preedit_changed), ed);
    gtk_selection_add_targets(widget, GDK_SELECTION_PRIMARY, text_targets, G_N_ELEMENTS(text_targets));
    gtk_selection_add_targets(widget, clipboard, text_targets, G_N_ELEMENTS(text_targets));
    gtk_drag_dest_set(widget, GTK_DEST_DEFAULT_ALL, drop_targets, G_N_ELEMENTS(drop_targets),
                      GDK_ACTION_COPY | GDK_ACTION_MOVE);
    relayout(ed);
}

GtkType
gtk_editor_get_type(void)
{
    static GtkType type;

    if (!type) {
        static const GtkTypeInfo info = {
            "GtkEditor", sizeof(GtkEditor), sizeof(GtkEditorClass),
            (GtkClassInitFunc) gtk_editor_class_init, (GtkObjectInitFunc) gtk_editor_init,
            NULL, NULL, NULL
        };

        type = gtk_type_unique(gtk_widget_get_type(), &info);
    }
    return type;
}

GtkWidget *
gtk_editor_new(void)
{
    return gtk_type_new(GTK_TYPE_EDITOR);
}

/* What a program asks of it. */

void
gtk_editor_set_text(GtkEditor *ed, const gchar *text, gssize len)
{
    gchar *valid;
    gsize n;

    g_return_if_fail(GTK_IS_EDITOR(ed));
    valid = clean(text ? text : "", text ? len : 0, &n);
    unmark(ed);
    text_set(ed->text, valid, n);
    g_free(valid);
    ed->cursor = ed->anchor = 0;
    ed->want_x = -1;
    ed->stale = TRUE;
    if (!ed->wrap) {  /* the widest line is this text's, not the last one's */
        ed->max_w = 0;
        relayout(ed);
    }
    settle(ed);
}

gchar *
gtk_editor_get_text(GtkEditor *ed, gsize start, gsize end)
{
    g_return_val_if_fail(GTK_IS_EDITOR(ed), NULL);
    end = MIN(end, ed->text->len);
    return text_copy(ed->text, MIN(start, end), end);
}

gsize
gtk_editor_get_length(GtkEditor *ed)
{
    g_return_val_if_fail(GTK_IS_EDITOR(ed), 0);
    return ed->text->len;
}

gsize
gtk_editor_get_lines(GtkEditor *ed)
{
    g_return_val_if_fail(GTK_IS_EDITOR(ed), 1);
    return ed->text->lines;
}

gsize
gtk_editor_line_of(GtkEditor *ed, gsize pos)
{
    g_return_val_if_fail(GTK_IS_EDITOR(ed), 0);
    return text_line_of(ed->text, MIN(pos, ed->text->len));
}

gsize
gtk_editor_line_start(GtkEditor *ed, gsize line)
{
    g_return_val_if_fail(GTK_IS_EDITOR(ed), 0);
    return text_line_start(ed->text, MIN(line, ed->text->lines - 1));
}

gsize
gtk_editor_get_cursor(GtkEditor *ed)
{
    g_return_val_if_fail(GTK_IS_EDITOR(ed), 0);
    return ed->cursor;
}

gboolean
gtk_editor_get_selection(GtkEditor *ed, gsize *start, gsize *end)
{
    gsize s, e;
    gboolean any;

    g_return_val_if_fail(GTK_IS_EDITOR(ed), FALSE);
    any = selected(ed, &s, &e);
    if (start)
        *start = s;
    if (end)
        *end = e;
    return any;
}

/*
 * A place gone to from elsewhere is not left at the very edge of the
 * view: a tenth of the view is seen above it, or below.
 */
static void
inset(GtkEditor *ed)
{
    GtkWidget *widget = GTK_WIDGET(ed);
    gint tall = widget->allocation.height, tenth = tall / 10, x, y, v = scroll_y(ed);
    gsize row;

    if (!ed->vadj || tall < 4 * ed->line_h)
        return;
    where(ed, ed->cursor, &row, &x);
    y = row * ed->line_h;
    if (y < v + tenth)
        v = y - tenth;
    else if (y + ed->line_h > v + tall - tenth)
        v = y + ed->line_h - tall + tenth;
    v = CLAMP(v, 0, MAX((gint) (ed->vadj->upper - ed->vadj->page_size), 0));
    if (v != (gint) ed->vadj->value)
        gtk_adjustment_set_value(ed->vadj, v);
}

void
gtk_editor_select(GtkEditor *ed, gsize start, gsize end)
{
    g_return_if_fail(GTK_IS_EDITOR(ed));
    ed->anchor = MIN(start, ed->text->len);
    /* The start is brought into view first, so that as much of it as fits is seen. */
    ed->cursor = ed->anchor;
    show_cursor(ed);
    move_to(ed, MIN(end, ed->text->len), TRUE, FALSE);
    inset(ed);
}

void
gtk_editor_insert(GtkEditor *ed, const gchar *text, gssize len)
{
    gchar *valid;
    gsize n;

    g_return_if_fail(GTK_IS_EDITOR(ed));
    valid = clean(text, len, &n);
    put_in(ed, valid, n, FALSE);
    g_free(valid);
}

void
gtk_editor_begin_action(GtkEditor *ed)
{
    g_return_if_fail(GTK_IS_EDITOR(ed));
    text_begin(ed->text);
}

void
gtk_editor_end_action(GtkEditor *ed)
{
    g_return_if_fail(GTK_IS_EDITOR(ed));
    text_end(ed->text);
}

gboolean
gtk_editor_can_undo(GtkEditor *ed)
{
    g_return_val_if_fail(GTK_IS_EDITOR(ed), FALSE);
    return text_can_undo(ed->text);
}

gboolean
gtk_editor_can_redo(GtkEditor *ed)
{
    g_return_val_if_fail(GTK_IS_EDITOR(ed), FALSE);
    return text_can_redo(ed->text);
}

gboolean
gtk_editor_get_modified(GtkEditor *ed)
{
    g_return_val_if_fail(GTK_IS_EDITOR(ed), FALSE);
    return text_modified(ed->text);
}

void
gtk_editor_set_saved(GtkEditor *ed)
{
    g_return_if_fail(GTK_IS_EDITOR(ed));
    text_saved(ed->text);
    gtk_signal_emit(GTK_OBJECT(ed), signals[CHANGED]);
}

gboolean
gtk_editor_find(GtkEditor *ed, const gchar *what, gboolean forward, gboolean match_case)
{
    gsize start, end, from;

    g_return_val_if_fail(GTK_IS_EDITOR(ed), FALSE);
    selected(ed, &start, &end);
    from = forward ? end : start;
    if (!text_find(ed->text, what, from, forward, match_case, &start, &end)
        && !text_find(ed->text, what, forward ? 0 : ed->text->len, forward, match_case, &start, &end))
        return FALSE;
    gtk_editor_select(ed, start, end);
    return TRUE;
}

gint
gtk_editor_replace_all(GtkEditor *ed, const gchar *what, const gchar *with, gboolean match_case)
{
    gsize n, first, count;
    gchar *valid;

    g_return_val_if_fail(GTK_IS_EDITOR(ed), 0);
    valid = clean(with, -1, &n);
    count = text_replace_all(ed->text, what, valid, n, match_case, &first);
    g_free(valid);
    if (count) {
        ed->cursor = ed->anchor = first + n;
        ed->want_x = -1;
        settle(ed);
    }
    return count;
}

void
gtk_editor_set_highlight(GtkEditor *ed, const gchar *what, gboolean match_case)
{
    const gchar *p;

    g_return_if_fail(GTK_IS_EDITOR(ed));
    unmark(ed);
    if (what && *what) {
        ed->mark = g_new(GdkWChar, strlen(what));
        ed->mark_case = match_case;
        for (p = what, ed->mark_n = 0; *p; p = g_utf8_next_char(p))
            ed->mark[ed->mark_n++] = match_case ? g_utf8_get_char(p) : g_unichar_tolower(g_utf8_get_char(p));
    }
    gtk_widget_queue_draw(GTK_WIDGET(ed));
}

/* A change to how the text is laid out, after which the cursor is where it was in the text. */
static void
laid_anew(GtkEditor *ed)
{
    relayout(ed);
    sized(ed);
    show_cursor(ed);
    gtk_widget_queue_draw(GTK_WIDGET(ed));
}

void
gtk_editor_set_font(GtkEditor *ed, GdkFont *font)
{
    g_return_if_fail(GTK_IS_EDITOR(ed));
    if (font)
        gdk_font_ref(font);
    if (ed->font)
        gdk_font_unref(ed->font);
    ed->font = font;
    laid_anew(ed);
}

void
gtk_editor_set_wrap(GtkEditor *ed, gboolean wrap)
{
    g_return_if_fail(GTK_IS_EDITOR(ed));
    ed->wrap = wrap != FALSE;
    if (ed->hadj && wrap)
        gtk_adjustment_set_value(ed->hadj, 0);
    laid_anew(ed);
}

void
gtk_editor_set_line_numbers(GtkEditor *ed, gboolean numbers)
{
    g_return_if_fail(GTK_IS_EDITOR(ed));
    ed->numbers = numbers != FALSE;
    laid_anew(ed);
}

void
gtk_editor_set_auto_indent(GtkEditor *ed, gboolean auto_indent)
{
    g_return_if_fail(GTK_IS_EDITOR(ed));
    ed->auto_indent = auto_indent != FALSE;
}

void
gtk_editor_set_tab_width(GtkEditor *ed, gint spaces)
{
    g_return_if_fail(GTK_IS_EDITOR(ed));
    ed->tab_width = CLAMP(spaces, 1, 64);
    laid_anew(ed);
}

gint
gtk_editor_get_tab_width(GtkEditor *ed)
{
    g_return_val_if_fail(GTK_IS_EDITOR(ed), 8);
    return ed->tab_width;
}
