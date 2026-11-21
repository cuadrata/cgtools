/* cgedit-text.c - cgedit's text, its lines, its undoing and its searching, with no window

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

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "textbuf.h"

static gint failed;

#define CHECK(what) \
    do { if (!(what)) { printf("line %d: %s\n", __LINE__, #what); failed++; } } while (0)

static gsize told_gone, told_come;

static void
told(TextBuf *text G_GNUC_UNUSED, gsize pos G_GNUC_UNUSED, gsize gone, gsize come, gpointer data G_GNUC_UNUSED)
{
    told_gone += gone;
    told_come += come;
}

/* The text is this string, and its lines start where this string's do. */
static gboolean
is(TextBuf *text, const gchar *want)
{
    gsize len = strlen(want), line = 0, i;

    if (text->len != len || memcmp(text_all(text), want, len) || text_line_start(text, 0) != 0)
        return FALSE;
    for (i = 0; i < len; i++)
        if (want[i] == '\n' && (++line >= text->lines || text_line_start(text, line) != i + 1
                                || text_line_end(text, line - 1) != i))
            return FALSE;
    return line + 1 == text->lines && text_line_end(text, line) == len;
}

/* A place in a string that is the start of a character, chosen at random. */
static gsize
somewhere(const gchar *s, gsize len)
{
    gsize pos = len ? (gsize) rand() % (len + 1) : 0;

    while (pos > 0 && pos < len && ((guchar) s[pos] & 0xc0) == 0x80)
        pos--;
    return pos;
}

int
main(void)
{
    static const gchar *const bits[] = {
        "a", "word", " ", "\n", "\n\n", "tab\there", "\303\251t\303\251", "\346\227\245\346\234\254",
        "line one\nline two\n", "\360\237\231\202", "x\ny\nz", ""
    };
    TextBuf *text = text_new(told, NULL);
    GPtrArray *were = g_ptr_array_new();
    GString *now = g_string_new(NULL);
    gsize pos, end, cursor, start, lines;
    gchar *s;
    guint i;
    gint step;

    setlocale(LC_ALL, "");

    /* An empty text is one line. */
    CHECK(is(text, "") && text->lines == 1 && !text_can_undo(text) && !text_modified(text));

    /*
     * Things are put in and taken out at random, and the text and its
     * lines are what a plain string says they should be after each;
     * then all of it is undone, a step at a time, and done again.
     */
    srand(20261121);
    for (step = 0; step < 3000; step++) {
        g_ptr_array_add(were, g_strdup(now->str));
        lines = text->lines;
        told_gone = told_come = 0;
        text_break(text);
        if (rand() % 3 && now->len) {
            pos = somewhere(now->str, now->len);
            end = somewhere(now->str, now->len);
            if (end < pos) {
                gsize t = pos;
                pos = end;
                end = t;
            }
            if (end - pos > 40)
                end = somewhere(now->str, pos + 40);
            if (end <= pos) {
                g_free(g_ptr_array_remove_index(were, were->len - 1));
                continue;
            }
            text_delete(text, pos, end - pos);
            g_string_erase(now, pos, end - pos);
        } else {
            const gchar *bit = bits[rand() % G_N_ELEMENTS(bits)];

            if (!*bit) {
                g_free(g_ptr_array_remove_index(were, were->len - 1));
                continue;
            }
            pos = somewhere(now->str, now->len);
            text_insert(text, pos, bit, strlen(bit));
            g_string_insert(now, pos, bit);
        }
        if (!is(text, now->str) || text->lines != lines - told_gone + told_come) {
            printf("step %d: the text is not what it should be\n", step);
            failed++;
            break;
        }
        pos = somewhere(now->str, now->len);
        if (text_line_start(text, text_line_of(text, pos)) > pos
            || text_line_end(text, text_line_of(text, pos)) < pos
            || (pos < (gsize) now->len && text_char(text, pos) != g_utf8_get_char(now->str + pos))
            || (pos < (gsize) now->len && text_next(text, pos) != (gsize) (g_utf8_next_char(now->str + pos) - now->str))
            || (pos > 0 && text_prev(text, pos) != (gsize) (g_utf8_prev_char(now->str + pos) - now->str))) {
            printf("step %d: a place in the text is not where it should be\n", step);
            failed++;
            break;
        }
    }
    CHECK(were->len > 1000 && text_modified(text));
    for (i = were->len; i-- > 0;)
        if (!text_undo(text, &cursor) || !is(text, were->pdata[i]) || cursor > text->len) {
            printf("undoing: step %u is not as it was\n", i);
            failed++;
            break;
        }
    CHECK(!text_can_undo(text) && is(text, "") && !text_modified(text));
    for (i = 1; i < were->len; i++)
        if (!text_redo(text, &cursor) || !is(text, were->pdata[i])) {
            printf("doing again: step %u is not as it was\n", i);
            failed++;
            break;
        }
    CHECK(text_redo(text, &cursor) && !text_can_redo(text) && is(text, now->str));
    for (i = 0; i < were->len; i++)
        g_free(were->pdata[i]);
    g_ptr_array_free(were, TRUE);
    g_string_free(now, TRUE);

    /* Typing is undone a word at a time, and rubbing out all at once. */
    text_set(text, "", 0);
    s = "hello big world";
    for (i = 0; s[i]; i++)
        text_insert(text, i, s + i, 1);
    CHECK(is(text, s) && text_modified(text));
    CHECK(text_undo(text, &cursor) && is(text, "hello big") && cursor == 9);
    CHECK(text_undo(text, &cursor) && is(text, "hello") && text_undo(text, &cursor) && is(text, ""));
    CHECK(!text_undo(text, &cursor) && !text_modified(text));
    CHECK(text_redo(text, &cursor) && text_redo(text, &cursor) && is(text, "hello big") && cursor == 9);
    for (i = 9; i > 6; i--)
        text_delete(text, i - 1, 1);
    CHECK(is(text, "hello ") && !text_can_redo(text));
    CHECK(text_undo(text, &cursor) && is(text, "hello big") && cursor == 9);

    /* Where the cursor goes elsewhere between, typing is two things. */
    text_set(text, "ab", 2);
    text_insert(text, 2, "c", 1);
    text_break(text);
    text_insert(text, 3, "d", 1);
    CHECK(text_undo(text, &cursor) && is(text, "abc") && text_undo(text, &cursor) && is(text, "ab"));

    /* Several things done as one are undone as one. */
    text_set(text, "one\ntwo\nthree", 13);
    text_begin(text);
    text_insert(text, text_line_start(text, 2), "\t", 1);
    text_insert(text, text_line_start(text, 1), "\t", 1);
    text_insert(text, text_line_start(text, 0), "\t", 1);
    text_end(text);
    CHECK(is(text, "\tone\n\ttwo\n\tthree"));
    CHECK(text_undo(text, &cursor) && is(text, "one\ntwo\nthree") && !text_can_undo(text));
    CHECK(text_redo(text, &cursor) && is(text, "\tone\n\ttwo\n\tthree"));

    /* Saved, changed, and undone back to how it was saved. */
    text_saved(text);
    CHECK(!text_modified(text));
    text_insert(text, 0, "x", 1);
    CHECK(text_modified(text) && text_undo(text, &cursor) && !text_modified(text));
    CHECK(text_undo(text, &cursor) && text_modified(text) && text_redo(text, &cursor) && !text_modified(text));
    text_undo(text, &cursor);
    text_insert(text, 0, "y", 1);  /* what was saved cannot be come back to now */
    CHECK(text_modified(text) && text_undo(text, &cursor) && text_modified(text));

    /* Looking for things: onwards, backwards, and with capitals aside. */
    s = "The cat, the CAT and the \303\211T\303\211 of caT\nthe end";
    text_set(text, s, strlen(s));
    CHECK(text_find(text, "cat", 0, TRUE, TRUE, &start, &end) && start == 4 && end == 7);
    CHECK(!text_find(text, "cat", 5, TRUE, TRUE, &start, &end));
    CHECK(text_find(text, "cat", 5, TRUE, FALSE, &start, &end) && start == 13 && end == 16);
    CHECK(text_find(text, "CAT", text->len, FALSE, FALSE, &start, &end) && start == 34 && end == 37);
    CHECK(text_find(text, "cat", 34, FALSE, FALSE, &start, &end) && start == 13);
    CHECK(text_find(text, "cat", 13, FALSE, TRUE, &start, &end) && start == 4);
    CHECK(!text_find(text, "cat", 4, FALSE, FALSE, &start, &end));
    /* Capitals beyond ASCII are known as the locale knows them: in the C locale, not at all. */
    if (g_unichar_tolower(0xc9) == 0xe9)
        CHECK(text_find(text, "\303\251t\303\251", 0, TRUE, FALSE, &start, &end) && start == 25 && end == 30);
    CHECK(!text_find(text, "\303\251t\303\251", 0, TRUE, TRUE, &start, &end));
    CHECK(text_find(text, "caT\nthe", 0, TRUE, TRUE, &start, &end) && start == 34 && end == 41);
    CHECK(text_find(text, "end", 0, TRUE, TRUE, &start, &end) && end == text->len);
    CHECK(!text_find(text, "", 0, TRUE, TRUE, &start, &end) && !text_find(text, "ends", 0, TRUE, FALSE, &start, &end));

    /* Every one of them made something else, as one thing to undo. */
    CHECK(text_replace_all(text, "the", "a", 1, FALSE, &start) == 4 && start == 0);
    CHECK(is(text, "a cat, a CAT and a \303\211T\303\211 of caT\na end"));
    CHECK(text_undo(text, &cursor) && is(text, s) && !text_can_undo(text));
    CHECK(text_replace_all(text, "the", "them and", 8, TRUE, &start) == 3 && start == 9);
    CHECK(is(text, "The cat, them and CAT and them and \303\211T\303\211 of caT\nthem and end") && text->lines == 2);
    CHECK(text_replace_all(text, "dog", "x", 1, TRUE, &start) == 0
          && text_replace_all(text, "", "x", 1, TRUE, &start) == 0);
    CHECK(text_replace_all(text, "\n", "", 0, TRUE, &start) == 1 && text->lines == 1);
    CHECK(text_replace_all(text, "a", "aa", 2, TRUE, &start) == 6);

    /* What is not UTF-8 is made so. */
    s = text_valid("ok\377\0bad\303", 8, &pos);
    CHECK(pos == 14 && !memcmp(s, "ok\357\277\275\357\277\275bad\357\277\275", 14) && g_utf8_validate(s, pos, NULL));
    g_free(s);
    text_set(text, "The cat", 7);
    s = text_copy(text, 4, 7);
    CHECK(!strcmp(s, "cat"));
    g_free(s);

    text_free(text);

    if (failed)
        printf("%d failed\n", failed);
    return failed != 0;
}
