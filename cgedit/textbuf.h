/* textbuf.h - text as an editor keeps it: bytes, lines, and what was done to them

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

#ifndef TEXTBUF_H
#define TEXTBUF_H

#include <glib.h>

typedef struct _TextBuf TextBuf;

/*
 * Told of every change once it is made, an undone or redone one too:
 * where it was, and how many line ends went from there and came.
 */
typedef void (*TextChanged)(TextBuf *text, gsize pos, gsize gone, gsize come, gpointer data);

/*
 * The text is UTF-8 with a gap in it where it was last changed, so
 * that typing moves little. Places in it are counted in bytes, and
 * are always where a character starts. The lines are known by where
 * each starts; the first starts at nought, and there is always one.
 * A change moves every line after it, which in a long text is many,
 * so they are not all moved at once: the lines from one on are still
 * to be moved, by so much, and a change near the last costs little.
 */
struct _TextBuf {
    gchar *bytes;
    gsize size, gap, gap_len, len;
    gsize *starts, lines, starts_size;
    gsize from;          /* the first line whose start is yet to be moved */
    gssize shift;        /* and how far on those are from where starts has them */
    GPtrArray *changes;  /* what was done, the earliest first */
    guint done;          /* how many of them stand; the rest were undone */
    glong saved;         /* how many stood when the text was saved, or -1 if that cannot be come back to */
    guint serial, group, grouped;
    gboolean joins;      /* the next change may be part of the last */
    TextChanged changed;
    gpointer data;
};

TextBuf *text_new(TextChanged changed, gpointer data);
void text_free(TextBuf *text);
gchar *text_valid(const gchar *s, gssize n, gsize *len);
void text_set(TextBuf *text, const gchar *s, gsize n);
const gchar *text_all(TextBuf *text);
const gchar *text_range(TextBuf *text, gsize start, gsize end);
gchar *text_copy(TextBuf *text, gsize start, gsize end);
gunichar text_char(const TextBuf *text, gsize pos);
gsize text_next(const TextBuf *text, gsize pos);
gsize text_prev(const TextBuf *text, gsize pos);
gsize text_line_of(const TextBuf *text, gsize pos);
gsize text_line_start(const TextBuf *text, gsize line);
gsize text_line_end(const TextBuf *text, gsize line);
void text_insert(TextBuf *text, gsize pos, const gchar *s, gsize n);
void text_delete(TextBuf *text, gsize pos, gsize n);
void text_begin(TextBuf *text);
void text_end(TextBuf *text);
void text_break(TextBuf *text);
gboolean text_can_undo(const TextBuf *text);
gboolean text_can_redo(const TextBuf *text);
gboolean text_undo(TextBuf *text, gsize *cursor);
gboolean text_redo(TextBuf *text, gsize *cursor);
void text_saved(TextBuf *text);
gboolean text_modified(const TextBuf *text);
gboolean text_find(TextBuf *text, const gchar *what, gsize from, gboolean forward,
                   gboolean match_case, gsize *start, gsize *end);
gsize text_replace_all(TextBuf *text, const gchar *what, const gchar *with, gsize n,
                       gboolean match_case, gsize *first);

#endif /* TEXTBUF_H */
