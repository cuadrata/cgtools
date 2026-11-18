/* cghex-document.c - tests cghex's document without a window

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

/* A file is opened, edited as the editor edits it, and looked at: each
 * case here is one that once left the wrong bytes behind. */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cghex.h"

/* What hex-document.c wants of the rest of the program. */
GdkFont *def_font;
gint def_group_type;
guint max_undo_depth = 100;
gboolean show_offsets_column;
void hex_document_set_menu_sensitivity(HexDocument *doc) { (void)doc; }
GtkWidget *gtk_hex_new(HexDocument *doc) { (void)doc; return NULL; }
GtkType gtk_hex_get_type(void) { return 0; }
void gtk_hex_set_group_type(GtkHex *gh, guint type) { (void)gh; (void)type; }
void gtk_hex_set_font(GtkHex *gh, GdkFont *font) { (void)gh; (void)font; }
void gtk_hex_show_offsets(GtkHex *gh, gboolean show) { (void)gh; (void)show; }

static const char path[] = "cghex-document.tmp";
static int failed;

static HexDocument *open_with(const char *bytes, size_t n)
{
    FILE *file = fopen(path, "wb");
    HexDocument *doc;

    if(!file || fwrite(bytes, 1, n, file) != n || fclose(file)) {
        perror(path);
        exit(99);
    }
    if(!(doc = hex_document_new(path))) {
        fprintf(stderr, "%s could not be opened as a document\n", path);
        exit(99);
    }
    return doc;
}

static void said(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    failed += !ok;
}

/* The document holds these n bytes and no others. */
static void holds(HexDocument *doc, const char *what, const char *want, size_t n)
{
    size_t i;
    int ok = doc->file_size == n;

    for(i = 0; ok && i < n; i++)
        ok = hex_document_get_byte(doc, (guint)i) == (guchar)want[i];
    said(ok, what);
    if(!ok) {
        printf("      it holds %u:", doc->file_size);
        for(i = 0; i < doc->file_size && i < 24; i++)
            printf(" %02x", hex_document_get_byte(doc, (guint)i));
        printf("\n      it should hold %u:", (unsigned)n);
        for(i = 0; i < n; i++)
            printf(" %02x", (guchar)want[i]);
        printf("\n");
    }
}

int main(void)
{
    HexDocument *doc;
    guint found;

    setvbuf(stdout, NULL, _IOLBF, 0);
    gtk_type_init();

    /* Replace, by as many bytes, where the last edit left the buffer's
     * gap among the bytes replaced. */
    doc = open_with("abcdefghijklmnop", 16);
    hex_document_delete_data(doc, 5, 1, TRUE);
    hex_document_set_data(doc, 2, 4, 4, (guchar *)"WXYZ", TRUE);
    holds(doc, "replace across the place of the last edit", "abWXYZhijklmnop", 15);
    hex_document_undo(doc);
    holds(doc, "and undo it", "abcdeghijklmnop", 15);
    hex_document_redo(doc);
    holds(doc, "and redo it", "abWXYZhijklmnop", 15);
    gtk_object_unref(GTK_OBJECT(doc));

    /* Past the end of the file every byte reads as nought, and Find
     * must not match what ends in noughts there. */
    doc = open_with("xxA", 3);
    found = 99;
    said(!hex_document_find_forward(doc, 0, (guchar *)"A\0\0", 3, &found) &&
         !hex_document_find_backward(doc, 3, (guchar *)"A\0\0", 3, &found),
         "find stops at the end of the file");
    said(hex_document_find_forward(doc, 0, (guchar *)"xA", 2, &found) && found == 1,
         "find forward finds what ends the file");
    found = 99;
    said(hex_document_find_backward(doc, 3, (guchar *)"xA", 2, &found) && found == 1,
         "find backward finds what ends the file");
    gtk_object_unref(GTK_OBJECT(doc));

    /* A byte inserted: undone it goes, and redone it comes back. */
    doc = open_with("abc", 3);
    hex_document_set_byte(doc, 'X', 1, TRUE, TRUE);
    holds(doc, "insert a byte", "aXbc", 4);
    hex_document_undo(doc);
    holds(doc, "undo it", "abc", 3);
    hex_document_redo(doc);
    holds(doc, "redo it", "aXbc", 4);
    hex_document_undo(doc);
    hex_document_redo(doc);
    hex_document_undo(doc);
    holds(doc, "and back and forth again", "abc", 3);
    gtk_object_unref(GTK_OBJECT(doc));

    doc = open_with("abc", 3);
    hex_document_set_byte(doc, 'X', 3, TRUE, TRUE);
    hex_document_undo(doc);
    hex_document_redo(doc);
    holds(doc, "add a byte at the end, undo and redo", "abcX", 4);
    gtk_object_unref(GTK_OBJECT(doc));

    /* The first hex digit typed in insert mode makes a byte whose other
     * half is nought, whatever was last where the byte is put. */
    doc = open_with("abc", 3);
    hex_document_set_byte(doc, 0xff, 1, TRUE, TRUE);
    hex_document_delete_data(doc, 1, 1, TRUE);
    hex_document_set_nibble(doc, 4, 1, FALSE, TRUE, TRUE);
    holds(doc, "insert a hex digit", "a\x40" "bc", 4);
    hex_document_set_nibble(doc, 2, 1, TRUE, TRUE, TRUE);
    holds(doc, "and its second digit", "a\x42" "bc", 4);
    hex_document_undo(doc);
    hex_document_undo(doc);
    holds(doc, "undo both", "abc", 3);
    hex_document_redo(doc);
    hex_document_redo(doc);
    holds(doc, "redo both", "a\x42" "bc", 4);
    gtk_object_unref(GTK_OBJECT(doc));

    /* More asked to be replaced than there is left. */
    doc = open_with("abc", 3);
    hex_document_set_data(doc, 2, 1, 3, (guchar *)"Z", TRUE);
    holds(doc, "replace more than is left", "abZ", 3);
    gtk_object_unref(GTK_OBJECT(doc));

    unlink(path);
    return failed != 0;
}
