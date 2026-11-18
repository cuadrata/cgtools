/* -*- mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* config.c - configuration loading/saving

   Copyright (C) 1997, 1998, 1999, 2000 Free Software Foundation

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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <gtk/gtk.h>

#include "cghex.h"

/*
 * The settings live in $XDG_CONFIG_HOME/cghex/cghexrc, ~/.config/cghex/cghexrc
 * when that is not set, as sections of key=value lines, the way
 * gnome-config kept them:
 *
 *   [Display]
 *   Font=Monospace 10
 */

gint def_group_type = GROUP_BYTE;
guint max_undo_depth;
gchar *offset_fmt = NULL;
gboolean show_offsets_column = TRUE;

/*
 * whether a format prints the offset: one conversion of it, as d, o, x
 * or X, and nothing else that takes an argument; bad_pos is where it
 * goes wrong
 */
gboolean offset_format_valid(const gchar *fmt, gint *bad_pos)
{
    gint i, conversions = 0;

    for(i = 0; fmt[i]; i++) {
        if(fmt[i] != '%')
            continue;
        i++;
        if(fmt[i] == '%')
            continue;
        while(fmt[i] && strchr("-+ #0123456789", fmt[i]))
            i++;
        if(!fmt[i] || !strchr("doxX", fmt[i]) || ++conversions > 1) {
            if(bad_pos)
                *bad_pos = fmt[i] ? i : i - 1;
            return FALSE;
        }
    }

    if(conversions != 1) {
        if(bad_pos)
            *bad_pos = 0;
        return FALSE;
    }

    return TRUE;
}

static gchar *config_dir(void)
{
    const gchar *xdg = getenv("XDG_CONFIG_HOME");

    if(xdg && *xdg == '/')
        return g_strconcat(xdg, "/cghex", NULL);

    return g_strconcat(g_get_home_dir(), "/.config/cghex", NULL);
}

static gchar *config_file(void)
{
    gchar *dir = config_dir();
    gchar *file = g_strconcat(dir, "/cghexrc", NULL);

    g_free(dir);

    return file;
}

/* the directory and those above it, as needed */
static gboolean make_dirs(const gchar *path)
{
    gchar *copy, *p;
    gboolean ok = TRUE;

    copy = g_strdup(path);
    for(p = copy + 1; ok; p++) {
        if(*p == '/' || *p == 0) {
            gchar c = *p;

            *p = 0;
            if(mkdir(copy, 0755) != 0 && errno != EEXIST)
                ok = FALSE;
            *p = c;
            if(c == 0)
                break;
        }
    }
    g_free(copy);

    return ok;
}

static void strip(gchar *s)
{
    gchar *end;

    g_strstrip(s);
    end = s + strlen(s);
    while(end > s && (end[-1] == '\n' || end[-1] == '\r'))
        *--end = 0;
}

/* reads the file into a table of "Section/Key" to value */
static GHashTable *config_read(void)
{
    GHashTable *table = g_hash_table_new(g_str_hash, g_str_equal);
    gchar *file, *section = NULL, *eq;
    gchar line[1024];
    FILE *f;

    file = config_file();
    f = fopen(file, "r");
    g_free(file);
    if(!f)
        return table;

    while(fgets(line, sizeof(line), f)) {
        strip(line);
        if(line[0] == 0 || line[0] == '#' || line[0] == ';')
            continue;

        if(line[0] == '[') {
            gchar *close = strchr(line, ']');

            if(close) {
                *close = 0;
                g_free(section);
                section = g_strdup(line + 1);
            }
            continue;
        }

        if(section && (eq = strchr(line, '=')) != NULL) {
            gchar *key, *old_key, *old_value;

            *eq = 0;
            strip(line);
            key = g_strconcat(section, "/", line, NULL);
            if(g_hash_table_lookup_extended(table, key, (gpointer *)&old_key,
                                            (gpointer *)&old_value)) {
                g_hash_table_remove(table, key);
                g_free(old_key);
                g_free(old_value);
            }
            g_hash_table_insert(table, key, g_strdup(eq + 1));
        }
    }
    g_free(section);
    fclose(f);

    return table;
}

static void config_free_entry(gpointer key, gpointer value, gpointer data G_GNUC_UNUSED)
{
    g_free(key);
    g_free(value);
}

static void config_free(GHashTable *table)
{
    g_hash_table_foreach(table, config_free_entry, NULL);
    g_hash_table_destroy(table);
}

static const gchar *config_get_string(GHashTable *table, const gchar *key,
                                      const gchar *def)
{
    const gchar *value = g_hash_table_lookup(table, key);

    return value ? value : def;
}

static gint config_get_int(GHashTable *table, const gchar *key, gint def)
{
    const gchar *value = g_hash_table_lookup(table, key);
    gchar *end;
    glong l;

    if(!value || !*value)
        return def;

    l = strtol(value, &end, 10);

    return *end == 0 ? (gint)l : def;
}

static gboolean config_get_bool(GHashTable *table, const gchar *key, gboolean def)
{
    const gchar *value = g_hash_table_lookup(table, key);

    if(!value)
        return def;

    return (g_strcasecmp(value, "true") == 0 || g_strcasecmp(value, "yes") == 0 ||
            strcmp(value, "1") == 0);
}

void save_configuration(void)
{
    gchar *dir, *file;
    FILE *f;

    dir = config_dir();
    if(!make_dirs(dir)) {
        g_free(dir);
        return;
    }
    file = config_file();
    f = fopen(file, "w");
    g_free(file);
    g_free(dir);
    if(!f)
        return;

    fprintf(f, "[Display]\n");
    if(def_font_name)
        fprintf(f, "Font=%s\n", def_font_name);
    fprintf(f, "Group=%d\n", def_group_type);
    fprintf(f, "\n[Editing]\n");
    fprintf(f, "MaxUndoDepth=%u\n", max_undo_depth);
    fprintf(f, "OffsetFormat=%s\n", offset_fmt);
    fprintf(f, "OffsetsColumn=%s\n", show_offsets_column ? "true" : "false");

    fclose(f);
}

void load_configuration(void)
{
    GHashTable *table;
    GdkFont *new_font;
    const gchar *font_desc;

    table = config_read();

    font_desc = config_get_string(table, "Display/Font", DEFAULT_FONT);
    if((new_font = gdk_font_load(font_desc)) == NULL && strcmp(font_desc, DEFAULT_FONT) != 0) {
        font_desc = DEFAULT_FONT;
        new_font = gdk_font_load(font_desc);
    }
    if(new_font) {
        if(def_font)
            gdk_font_unref(def_font);
        def_font = new_font;
        g_free(def_font_name);
        def_font_name = g_strdup(font_desc);
    }

    def_group_type = config_get_int(table, "Display/Group", GROUP_BYTE);
    if(def_group_type != GROUP_BYTE && def_group_type != GROUP_WORD &&
       def_group_type != GROUP_LONG)
        def_group_type = GROUP_BYTE;

    max_undo_depth = CLAMP(config_get_int(table, "Editing/MaxUndoDepth", 100),
                           0, MAX_MAX_UNDO_DEPTH);

    g_free(offset_fmt);
    offset_fmt = g_strdup(config_get_string(table, "Editing/OffsetFormat", "%x"));
    if(!offset_format_valid(offset_fmt, NULL)) {
        g_free(offset_fmt);
        offset_fmt = g_strdup("%x");
    }

    show_offsets_column = config_get_bool(table, "Editing/OffsetsColumn", TRUE);

    config_free(table);
}
