/* textbuf.c - text as an editor keeps it: bytes, lines, and what was done to them

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

#include <string.h>

#include "textbuf.h"

enum { SPARE = 4096 };  /* the least room made when there is none */

/* Some bytes, which need be no string. */
typedef struct {
    gchar *s;
    gsize len;
} Bytes;

/* Something done to the text: at a place, what went from it and what came. */
typedef struct {
    gsize pos;
    Bytes gone, came;
    guint group;  /* what is done together is undone together */
} Change;

/* More bytes after the others, or before them. */
static void
bytes_add(Bytes *b, const gchar *s, gsize n, gboolean before)
{
    if (!n)
        return;
    b->s = g_realloc(b->s, b->len + n + 1);
    if (before)
        memmove(b->s + n, b->s, b->len);
    memcpy(b->s + (before ? 0 : b->len), s, n);
    b->len += n;
}

static void
change_free(Change *c)
{
    g_free(c->gone.s);
    g_free(c->came.s);
    g_free(c);
}

/* What was undone is forgotten, or with all of it what was done too. */
static void
forget(TextBuf *text, guint keep)
{
    while (text->changes->len > keep)
        change_free(g_ptr_array_remove_index(text->changes, text->changes->len - 1));
    if (text->done > keep)
        text->done = keep;
    if (text->saved > (glong) keep)
        text->saved = -1;
}

TextBuf *
text_new(TextChanged changed, gpointer data)
{
    TextBuf *text = g_new0(TextBuf, 1);

    text->size = text->gap_len = SPARE;
    text->bytes = g_malloc(text->size);
    text->starts_size = 64;
    text->starts = g_new0(gsize, text->starts_size);
    text->lines = 1;
    text->changes = g_ptr_array_new();
    text->changed = changed;
    text->data = data;
    return text;
}

void
text_free(TextBuf *text)
{
    forget(text, 0);
    g_ptr_array_free(text->changes, TRUE);
    g_free(text->starts);
    g_free(text->bytes);
    g_free(text);
}

static gchar
byte_at(const TextBuf *text, gsize i)
{
    return text->bytes[i < text->gap ? i : i + text->gap_len];
}

static void
gap_to(TextBuf *text, gsize pos)
{
    if (pos < text->gap)
        memmove(text->bytes + pos + text->gap_len, text->bytes + pos, text->gap - pos);
    else if (pos > text->gap)
        memmove(text->bytes + text->gap, text->bytes + text->gap + text->gap_len, pos - text->gap);
    text->gap = pos;
}

/* There is room for so much more, and for a nul after it all. */
static void
room(TextBuf *text, gsize n)
{
    if (text->gap_len > n)
        return;
    gap_to(text, text->len);
    text->size = text->len + n + MAX(text->len / 2, SPARE);
    text->bytes = g_realloc(text->bytes, text->size);
    text->gap_len = text->size - text->len;
}

/* The whole text in one piece, with a nul after it; good until the next change. */
const gchar *
text_all(TextBuf *text)
{
    gap_to(text, text->len);
    text->bytes[text->len] = '\0';
    return text->bytes;
}

/* Part of it in one piece, with no nul after it; good until the next change. */
const gchar *
text_range(TextBuf *text, gsize start, gsize end)
{
    if (text->gap > start && text->gap < end)
        gap_to(text, end);
    return text->bytes + (text->gap <= start ? start + text->gap_len : start);
}

/* Part of it, to be freed. */
gchar *
text_copy(TextBuf *text, gsize start, gsize end)
{
    gchar *copy = g_malloc(end - start + 1);

    memcpy(copy, text_range(text, start, end), end - start);
    copy[end - start] = '\0';
    return copy;
}

/* The character that starts at a place; nought at the end. */
gunichar
text_char(const TextBuf *text, gsize pos)
{
    guchar c;
    gunichar u;
    gint more;

    if (pos >= text->len)
        return 0;
    c = byte_at(text, pos);
    if (c < 0x80)
        return c;
    more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1;
    u = c & (0x3f >> more);
    while (more-- && ++pos < text->len)
        u = u << 6 | (byte_at(text, pos) & 0x3f);
    return u;
}

/* Where the next character starts, and the one before. */
gsize
text_next(const TextBuf *text, gsize pos)
{
    if (pos < text->len)
        while (++pos < text->len && (byte_at(text, pos) & 0xc0) == 0x80)
            ;
    return pos;
}

gsize
text_prev(const TextBuf *text, gsize pos)
{
    if (pos > 0)
        while (--pos > 0 && (byte_at(text, pos) & 0xc0) == 0x80)
            ;
    return pos;
}

static gsize
start_of(const TextBuf *text, gsize line)
{
    return line < text->from ? text->starts[line] : text->starts[line] + text->shift;
}

/* The lines yet to be moved are those from this one on. */
static void
shift_from(TextBuf *text, gsize line)
{
    gsize j;

    for (j = text->from; j < line; j++)
        text->starts[j] += text->shift;
    for (j = line; j < text->from; j++)
        text->starts[j] -= text->shift;
    text->from = line;
}

/* The line a place is in: the last that starts at or before it. */
gsize
text_line_of(const TextBuf *text, gsize pos)
{
    gsize lo = 0, hi = text->lines - 1, mid;

    while (lo < hi) {
        mid = (lo + hi + 1) / 2;
        if (start_of(text, mid) <= pos)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

gsize
text_line_start(const TextBuf *text, gsize line)
{
    return start_of(text, line);
}

/* Where a line ends: before its line end, or at the end of the text. */
gsize
text_line_end(const TextBuf *text, gsize line)
{
    return line + 1 < text->lines ? start_of(text, line + 1) - 1 : text->len;
}

/* The bytes go in, and the lines after them are further on, with new ones among them. */
static void
put(TextBuf *text, gsize pos, const gchar *s, gsize n)
{
    gsize line = text_line_of(text, pos), come = 0, i, j;

    if (!n)
        return;
    room(text, n);
    gap_to(text, pos);
    memcpy(text->bytes + pos, s, n);
    text->gap += n;
    text->gap_len -= n;
    text->len += n;

    for (i = 0; i < n; i++)
        come += s[i] == '\n';
    if (text->lines + come > text->starts_size) {
        text->starts_size = (text->lines + come) * 3 / 2;
        text->starts = g_renew(gsize, text->starts, text->starts_size);
    }
    shift_from(text, line + 1);
    if (come)
        memmove(text->starts + line + 1 + come, text->starts + line + 1,
                (text->lines - line - 1) * sizeof *text->starts);
    for (i = 0, j = line + 1; i < n; i++)
        if (s[i] == '\n')
            text->starts[j++] = pos + i + 1;
    text->lines += come;
    text->from = line + 1 + come;
    text->shift += n;
    if (text->changed)
        text->changed(text, pos, 0, come, text->data);
}

/* The bytes come out, and the lines that started among them are no more. */
static void
take(TextBuf *text, gsize pos, gsize n)
{
    gsize line = text_line_of(text, pos), gone = 0;

    if (!n)
        return;
    gap_to(text, pos);
    text->gap_len += n;
    text->len -= n;

    shift_from(text, line + 1);
    while (line + 1 + gone < text->lines && start_of(text, line + 1 + gone) <= pos + n)
        gone++;
    if (gone)
        memmove(text->starts + line + 1, text->starts + line + 1 + gone,
                (text->lines - line - 1 - gone) * sizeof *text->starts);
    text->lines -= gone;
    text->shift -= n;
    if (text->changed)
        text->changed(text, pos, gone, 0, text->data);
}

/*
 * What is not UTF-8 is made so, to be freed: a byte that is part of
 * no character, and a nul, which would end the text early for much
 * that reads it, each become the character that stands for one that
 * cannot be shown.
 */
gchar *
text_valid(const gchar *s, gssize n, gsize *len)
{
    GByteArray *valid = g_byte_array_new();
    const gchar *end, *stop = s + (n < 0 ? (gssize) strlen(s) : n);
    gchar *bytes;

    while (s < stop) {
        g_utf8_validate(s, stop - s, &end);
        g_byte_array_append(valid, (const guint8 *) s, end - s);
        if (end < stop) {
            g_byte_array_append(valid, (const guint8 *) "\357\277\275", 3);
            end++;
        }
        s = end;
    }
    if (len)
        *len = valid->len;
    g_byte_array_append(valid, (const guint8 *) "", 1);
    bytes = (gchar *) valid->data;
    g_byte_array_free(valid, FALSE);
    return bytes;
}

/* All the text is other text, which is as it was saved, with nothing done to it. */
void
text_set(TextBuf *text, const gchar *s, gsize n)
{
    forget(text, 0);
    take(text, 0, text->len);
    put(text, 0, s, n);
    text->saved = 0;
    text->joins = FALSE;
}

static gboolean
blank(gchar c)
{
    return c == ' ' || c == '\t' || c == '\n';
}

/*
 * A change is written down before it is made. One character typed
 * after another is part of the same change, and so is one rubbed out
 * after another, either way; but a space after a word starts a new
 * one, which the next word is part of, so that undoing goes back a
 * word at a time.
 */
static void
record(TextBuf *text, gsize pos, const gchar *gone, gsize gone_len, const gchar *came, gsize came_len)
{
    Change *last = text->done ? text->changes->pdata[text->done - 1] : NULL, *c;
    const gchar *what = came_len ? came : gone;
    gboolean one = !text->grouped && g_utf8_next_char(what) == what + came_len + gone_len;

    forget(text, text->done);
    if (last && text->joins && one) {
        gboolean joined = TRUE;

        if (came_len && !last->gone.len && pos == last->pos + last->came.len
            && !(blank(*came) && !blank(last->came.s[last->came.len - 1])))
            bytes_add(&last->came, came, came_len, FALSE);
        else if (gone_len && !last->came.len && pos + gone_len == last->pos) {
            bytes_add(&last->gone, gone, gone_len, TRUE);
            last->pos = pos;
        } else if (gone_len && !last->came.len && pos == last->pos)
            bytes_add(&last->gone, gone, gone_len, FALSE);
        else
            joined = FALSE;
        if (joined) {
            if (text->saved == (glong) text->done)
                text->saved = -1;
            return;
        }
    }
    c = g_new0(Change, 1);
    c->pos = pos;
    bytes_add(&c->gone, gone, gone_len, FALSE);
    bytes_add(&c->came, came, came_len, FALSE);
    c->group = text->grouped ? text->group : ++text->serial;
    g_ptr_array_add(text->changes, c);
    text->done++;
    text->joins = one;
}

void
text_insert(TextBuf *text, gsize pos, const gchar *s, gsize n)
{
    if (!n)
        return;
    record(text, pos, NULL, 0, s, n);
    put(text, pos, s, n);
}

void
text_delete(TextBuf *text, gsize pos, gsize n)
{
    if (!n)
        return;
    record(text, pos, text_range(text, pos, pos + n), n, NULL, 0);
    take(text, pos, n);
}

/* What is done from here to text_end() is one thing to undo. */
void
text_begin(TextBuf *text)
{
    if (!text->grouped++)
        text->group = ++text->serial;
    text->joins = FALSE;
}

void
text_end(TextBuf *text)
{
    text->grouped--;
    text->joins = FALSE;
}

/* The next change is one of its own: the cursor went elsewhere. */
void
text_break(TextBuf *text)
{
    text->joins = FALSE;
}

gboolean
text_can_undo(const TextBuf *text)
{
    return text->done > 0;
}

gboolean
text_can_redo(const TextBuf *text)
{
    return text->done < text->changes->len;
}

/* The last thing done is undone; where the cursor belongs after it. */
gboolean
text_undo(TextBuf *text, gsize *cursor)
{
    Change *c;
    guint group;

    if (!text->done)
        return FALSE;
    group = ((Change *) text->changes->pdata[text->done - 1])->group;
    do {
        c = text->changes->pdata[--text->done];
        take(text, c->pos, c->came.len);
        put(text, c->pos, c->gone.s, c->gone.len);
        *cursor = c->pos + c->gone.len;
    } while (text->done && ((Change *) text->changes->pdata[text->done - 1])->group == group);
    text->joins = FALSE;
    return TRUE;
}

/* And done again. */
gboolean
text_redo(TextBuf *text, gsize *cursor)
{
    Change *c;
    guint group;

    if (text->done >= text->changes->len)
        return FALSE;
    group = ((Change *) text->changes->pdata[text->done])->group;
    do {
        c = text->changes->pdata[text->done++];
        take(text, c->pos, c->gone.len);
        put(text, c->pos, c->came.s, c->came.len);
        *cursor = c->pos + c->came.len;
    } while (text->done < text->changes->len
             && ((Change *) text->changes->pdata[text->done])->group == group);
    text->joins = FALSE;
    return TRUE;
}

/* The text is as its file has it now; undoing back to here makes it so again. */
void
text_saved(TextBuf *text)
{
    text->saved = text->done;
    text->joins = FALSE;
}

gboolean
text_modified(const TextBuf *text)
{
    return text->saved != (glong) text->done;
}

/* How much of the text at p is what is looked for, capitals aside; nought if it is not. */
static gsize
same_at(const gchar *p, const gchar *end, const gunichar *what, gint n)
{
    const gchar *q = p;
    gint i;

    for (i = 0; i < n; i++, q = g_utf8_next_char(q))
        if (q >= end || g_unichar_tolower(g_utf8_get_char(q)) != what[i])
            return 0;
    return q - p;
}

/*
 * Something is looked for in text that is in one piece: onwards from
 * a place, or backwards for what starts before it. With capitals
 * aside, each character is taken as its small letter, on both sides;
 * small is then what is looked for in small letters, n of them.
 */
static gboolean
find_in(const gchar *s, gsize len, const gchar *what, const gunichar *small, gint chars,
        gsize from, gboolean forward, gsize *start, gsize *end)
{
    const gchar *stop = s + len, *p = s + MIN(from, len);
    gsize n = strlen(what), got = 0;

    if (!forward && p > s)
        p = g_utf8_prev_char(p);
    else if (!forward)
        p = NULL;
    while (p && p < stop) {
        if (!small)
            got = (gsize) (stop - p) >= n && !memcmp(p, what, n) ? n : 0;
        else
            got = same_at(p, stop, small, chars);
        if (got)
            break;
        if (forward) {
            /* Onwards, what cannot start a match in any case is passed over quickly. */
            p = g_utf8_next_char(p);
            if (!small && p < stop && !(p = memchr(p, *what, stop - p)))
                break;
        } else
            p = p > s ? g_utf8_prev_char(p) : NULL;
    }
    if (!got || !p || p >= stop)
        return FALSE;
    *start = p - s;
    *end = *start + got;
    return TRUE;
}

/* What is looked for in small letters, to be freed, if capitals are aside; else NULL. */
static gunichar *
small_of(const gchar *what, gboolean match_case, gint *chars)
{
    gunichar *small;

    *chars = 0;
    if (match_case)
        return NULL;
    small = g_new(gunichar, strlen(what) + 1);
    for (; *what; what = g_utf8_next_char(what))
        small[(*chars)++] = g_unichar_tolower(g_utf8_get_char(what));
    return small;
}

gboolean
text_find(TextBuf *text, const gchar *what, gsize from, gboolean forward,
          gboolean match_case, gsize *start, gsize *end)
{
    gunichar *small;
    gboolean found;
    gint chars;

    if (!*what || strlen(what) > text->len)
        return FALSE;
    small = small_of(what, match_case, &chars);
    found = find_in(text_all(text), text->len, what, small, chars, from, forward, start, end);
    g_free(small);
    return found;
}

/*
 * Every place something is, it becomes something else, as one thing
 * to undo: how many there were, and where the first was. They are
 * all found first and then changed from the last back to the first,
 * so that the gap in the text only ever moves one way.
 */
gsize
text_replace_all(TextBuf *text, const gchar *what, const gchar *with, gsize n, gboolean match_case, gsize *first)
{
    GArray *found = g_array_new(FALSE, FALSE, sizeof(gsize));
    gsize from = 0, start, end, count, i;
    gunichar *small;
    const gchar *s;
    gint chars;

    if (*what && strlen(what) <= text->len) {
        small = small_of(what, match_case, &chars);
        s = text_all(text);
        while (find_in(s, text->len, what, small, chars, from, TRUE, &start, &end)) {
            g_array_append_val(found, start);
            g_array_append_val(found, end);
            from = end;
        }
        g_free(small);
    }
    count = found->len / 2;
    if (count) {
        *first = g_array_index(found, gsize, 0);
        text_begin(text);
        for (i = count; i-- > 0;) {
            start = g_array_index(found, gsize, 2 * i);
            text_delete(text, start, g_array_index(found, gsize, 2 * i + 1) - start);
            text_insert(text, start, with, n);
        }
        text_end(text);
    }
    g_array_free(found, TRUE);
    return count;
}
