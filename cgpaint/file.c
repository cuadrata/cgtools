/* file.c - pictures in files: Windows bitmaps, XPM and PNG

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

#include <ctype.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <png.h>

#include "cgpaint.h"

/* The characters an XPM file names its colours with, in their usual order. */
static const gchar xpm_chars[] =
    " .XoO+@#$%&*=-;:>,<1234567890qwertyuipasdfghjklzxcvbnmMNBVCZASDFGHJKLPIUYTREWQ!~^/()_`'][{}|";

/* What kind of file a name says it is, by how it ends. */
gint
file_format(const gchar *name)
{
    const gchar *dot = strrchr(name, '.');

    if (!dot || strchr(dot, '/'))
        return FORMAT_NONE;
    if (!g_ascii_strcasecmp(dot, ".bmp") || !g_ascii_strcasecmp(dot, ".dib"))
        return FORMAT_BMP;
    if (!g_ascii_strcasecmp(dot, ".xpm"))
        return FORMAT_XPM;
    if (!g_ascii_strcasecmp(dot, ".png"))
        return FORMAT_PNG;
    return FORMAT_NONE;
}

static Image *
sized(gint w, gint h, GError **error)
{
    if (w < 1 || h < 1 || w > IMAGE_MAX || h > IMAGE_MAX) {
        g_set_error(error, FILE_ERROR, 0, _("The picture is too large, or has no size."));
        return NULL;
    }
    return image_new(w, h, 0xffffff);
}

static guint
le16(const guchar *p)
{
    return p[0] | p[1] << 8;
}

static guint32
le32(const guchar *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (guint32) p[3] << 24;
}

/*
 * A Windows bitmap: one that is not packed, of 1, 4, 8, 24 or 32 bits
 * a point, with the header of Windows 3 or a later one, or OS/2's.
 */
static Image *
load_bmp(const guchar *d, gsize len, GError **error)
{
    gsize head, bits, stride, colours, entry;
    gint32 w, h;
    gint bpp, x, y;
    gboolean down = FALSE;
    guint32 packed = 0;
    const guchar *pal;
    Image *img;

    if (len < 26 || (head = le32(d + 14)) > len - 14)
        goto bad;
    if (head == 12) {
        w = le16(d + 18);
        h = le16(d + 20);
        bpp = le16(d + 24);
        entry = 3;
    } else if (head >= 40) {
        w = (gint32) le32(d + 18);
        h = (gint32) le32(d + 22);
        bpp = le16(d + 28);
        packed = le32(d + 30);
        entry = 4;
        if (h < 0 && h > -IMAGE_MAX - 1) {
            h = -h;
            down = TRUE;
        }
    } else
        goto bad;
    if (packed == 3 && bpp == 32 && len >= 66
        && le32(d + 54) == 0xff0000 && le32(d + 58) == 0xff00 && le32(d + 62) == 0xff)
        packed = 0;
    if (packed != 0 || (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32)) {
        g_set_error(error, FILE_ERROR, 0,
                    _("Only bitmaps of 1, 4, 8, 24 or 32 bits that are not packed are read."));
        return NULL;
    }
    if (!(img = sized(w, h, error)))
        return NULL;
    bits = le32(d + 10);
    stride = ((gsize) w * bpp + 31) / 32 * 4;
    if (bits < 14 + head || bits > len || stride * h > len - bits) {
        image_free(img);
        goto bad;
    }
    /* Fewer colours than there could be is usual: then as many as there are. */
    pal = d + 14 + head;
    colours = bpp <= 8 ? MIN((gsize) 1 << bpp, (bits - 14 - head) / entry) : 0;
    for (y = 0; y < h; y++) {
        const guchar *row = d + bits + stride * (down ? y : h - 1 - y), *c;
        guchar *p = img->px + (gsize) y * w * 3;

        for (x = 0; x < w; x++, p += 3) {
            if (bpp >= 24)
                c = row + (gsize) x * (bpp / 8);
            else {
                guint i = bpp == 8 ? row[x]
                    : bpp == 4 ? (row[x / 2] >> (x & 1 ? 0 : 4)) & 15
                    : (row[x / 8] >> (7 - (x & 7))) & 1;

                if (i >= colours)
                    continue;
                c = pal + i * entry;
            }
            p[0] = c[2];
            p[1] = c[1];
            p[2] = c[0];
        }
    }
    return img;

bad:
    g_set_error(error, FILE_ERROR, 0, _("The bitmap is damaged."));
    return NULL;
}

static void
put32(GByteArray *out, guint32 v)
{
    guchar b[4] = { v, v >> 8, v >> 16, v >> 24 };

    g_byte_array_append(out, b, 4);
}

/* A bitmap of 24 bits a point, which everything reads. */
static GByteArray *
bmp_data(const Image *img)
{
    GByteArray *out = g_byte_array_new();
    gsize stride = ((gsize) img->w * 3 + 3) / 4 * 4;
    guchar *row;
    gint x, y;

    g_byte_array_append(out, (const guchar *) "BM", 2);
    put32(out, 54 + stride * img->h);
    put32(out, 0);
    put32(out, 54);
    put32(out, 40);
    put32(out, img->w);
    put32(out, img->h);
    put32(out, 1 | 24 << 16);  /* one plane, 24 bits */
    put32(out, 0);
    put32(out, stride * img->h);
    put32(out, 2835);  /* 72 points to the inch, in points to the metre */
    put32(out, 2835);
    put32(out, 0);
    put32(out, 0);
    row = g_malloc0(stride);
    for (y = img->h - 1; y >= 0; y--) {
        const guchar *p = img->px + (gsize) y * img->w * 3;

        for (x = 0; x < img->w; x++, p += 3) {
            row[x * 3] = p[2];
            row[x * 3 + 1] = p[1];
            row[x * 3 + 2] = p[0];
        }
        g_byte_array_append(out, row, stride);
    }
    g_free(row);
    return out;
}

/* What an XPM file's array holds: its strings, one after another. */
static GPtrArray *
xpm_strings(const gchar *d, gsize len)
{
    GPtrArray *strings = g_ptr_array_new();
    const gchar *p = d, *end = d + len;

    while (p < end)
        if (p + 1 < end && p[0] == '/' && p[1] == '*') {
            for (p += 2; p + 1 < end && !(p[0] == '*' && p[1] == '/'); p++)
                ;
            p += 2;
        } else if (*p == '"') {
            GString *s = g_string_new(NULL);

            for (p++; p < end && *p != '"'; p++) {
                if (*p == '\\' && p + 1 < end)
                    p++;
                g_string_append_c(s, *p);
            }
            p++;
            g_ptr_array_add(strings, s->str);
            g_string_free(s, FALSE);
        } else
            p++;
    return strings;
}

/*
 * The colour an XPM file gives its characters: what follows a "c",
 * failing that a "g" or an "m", which may be several words. None,
 * which is seen through, is white, as Paint had nothing seen through.
 */
static Rgb
xpm_colour(const gchar *spec)
{
    static const gchar *const keys[] = { "c", "g", "g4", "m", "s" };
    gchar **words = g_strsplit(spec, " ", 0), *found[G_N_ELEMENTS(keys)] = { NULL };
    GdkColor colour;
    Rgb rgb = 0;
    gint i, k, key = -1;

    for (i = 0; words[i]; i++) {
        gchar *word = g_strstrip(words[i]);

        if (!*word)
            continue;
        for (k = 0; k < (gint) G_N_ELEMENTS(keys) && strcmp(word, keys[k]); k++)
            ;
        if (k < (gint) G_N_ELEMENTS(keys) && (key < 0 || found[key]))
            key = k;
        else if (key >= 0) {
            gchar *more = found[key] ? g_strconcat(found[key], " ", word, NULL) : g_strdup(word);

            g_free(found[key]);
            found[key] = more;
        }
    }
    for (k = 0; k < 4 && !found[k]; k++)
        ;
    if (k < 4) {
        if (!g_ascii_strcasecmp(found[k], "None"))
            rgb = 0xffffff;
        else if (gdk_color_parse(found[k], &colour))
            rgb = (Rgb) (colour.red >> 8) << 16 | (colour.green >> 8) << 8 | colour.blue >> 8;
    }
    for (k = 0; k < (gint) G_N_ELEMENTS(keys); k++)
        g_free(found[k]);
    g_strfreev(words);
    return rgb;
}

static void
free_key(gpointer key, gpointer value G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
    g_free(key);
}

static Image *
load_xpm(const gchar *d, gsize len, GError **error)
{
    GPtrArray *strings = xpm_strings(d, len);
    GHashTable *colours = g_hash_table_new(g_str_hash, g_str_equal);
    gint w, h, n, cpp, i, x, y;
    Image *img = NULL;
    gchar key[9];

    if (strings->len < 1 || sscanf(strings->pdata[0], "%d %d %d %d", &w, &h, &n, &cpp) != 4
        || n < 1 || cpp < 1 || cpp > 8 || h < 1 || (guint) n >= strings->len
        || (guint) h > strings->len - 1 - n) {
        g_set_error(error, FILE_ERROR, 0, _("The XPM file is damaged."));
        goto out;
    }
    if (!(img = sized(w, h, error)))
        goto out;
    for (i = 1; i <= n; i++) {
        const gchar *s = strings->pdata[i];

        if (strlen(s) < (gsize) cpp)
            continue;
        g_hash_table_insert(colours, g_strndup(s, cpp), GUINT_TO_POINTER(xpm_colour(s + cpp) + 1));
    }
    key[cpp] = '\0';
    for (y = 0; y < h; y++) {
        const gchar *s = strings->pdata[1 + n + y];
        gsize sl = strlen(s);
        guchar *p = img->px + (gsize) y * w * 3;

        for (x = 0; x < w && (gsize) (x + 1) * cpp <= sl; x++, p += 3) {
            guint rgb;

            memcpy(key, s + (gsize) x * cpp, cpp);
            if ((rgb = GPOINTER_TO_UINT(g_hash_table_lookup(colours, key)))) {
                p[0] = (rgb - 1) >> 16;
                p[1] = (rgb - 1) >> 8;
                p[2] = rgb - 1;
            }
        }
    }

out:
    g_hash_table_foreach(colours, free_key, NULL);
    g_hash_table_destroy(colours);
    for (i = 0; i < (gint) strings->len; i++)
        g_free(strings->pdata[i]);
    g_ptr_array_free(strings, TRUE);
    return img;
}

/*
 * An XPM file: its colours, each with as many characters to its name
 * as they all need, and its points by those names. The array is named
 * after the file, as far as C lets it be.
 */
static GString *
xpm_data(const Image *img, const gchar *name)
{
    GHashTable *seen = g_hash_table_new(g_direct_hash, g_direct_equal);
    GArray *colours = g_array_new(FALSE, FALSE, sizeof(Rgb));
    GString *out = g_string_new("/* XPM */\n");
    gchar *base = g_path_get_basename(name), *dot = strrchr(base, '.'), *c, code[8];
    gsize i, points = (gsize) img->w * img->h;
    gint cpp = 1, k, x, y;
    guint n;

    for (i = 0; i < points; i++) {
        Rgb rgb = (Rgb) img->px[i * 3] << 16 | img->px[i * 3 + 1] << 8 | img->px[i * 3 + 2];

        if (!g_hash_table_lookup(seen, GUINT_TO_POINTER(rgb + 1))) {
            g_array_append_val(colours, rgb);
            g_hash_table_insert(seen, GUINT_TO_POINTER(rgb + 1), GUINT_TO_POINTER(colours->len));
        }
    }
    for (n = sizeof xpm_chars - 1; n < colours->len; n *= sizeof xpm_chars - 1)
        cpp++;

    if (dot && dot != base)
        *dot = '\0';
    for (c = base; *c; c++)
        if (!isalnum((guchar) *c) || (guchar) *c > 127)
            *c = '_';
    g_string_sprintfa(out, "static char *%s%s_xpm[] = {\n\"%d %d %u %d\",\n",
                      isdigit((guchar) *base) ? "_" : "", base, img->w, img->h, colours->len, cpp);
    g_free(base);

    code[cpp] = '\0';
    for (i = 0; i < colours->len; i++) {
        for (k = cpp - 1, n = i; k >= 0; k--, n /= sizeof xpm_chars - 1)
            code[k] = xpm_chars[n % (sizeof xpm_chars - 1)];
        g_string_sprintfa(out, "\"%s c #%06X\",\n", code, (guint) g_array_index(colours, Rgb, i));
    }
    for (y = 0; y < img->h; y++) {
        const guchar *p = img->px + (gsize) y * img->w * 3;

        g_string_append_c(out, '"');
        for (x = 0; x < img->w; x++, p += 3) {
            n = GPOINTER_TO_UINT(g_hash_table_lookup(seen, GUINT_TO_POINTER(((Rgb) p[0] << 16 | p[1] << 8 | p[2]) + 1))) - 1;
            for (k = cpp - 1; k >= 0; k--, n /= sizeof xpm_chars - 1)
                code[k] = xpm_chars[n % (sizeof xpm_chars - 1)];
            g_string_append(out, code);
        }
        g_string_append(out, y < img->h - 1 ? "\",\n" : "\"\n");
    }
    g_string_append(out, "};\n");
    g_array_free(colours, TRUE);
    g_hash_table_destroy(seen);
    return out;
}

/* A PNG file's points as CGTK reads them, laid on white. */
static Image *
from_rgba(guchar *rgba, gint w, gint h, GError **error)
{
    Image *img = rgba ? sized(w, h, error) : NULL;
    gsize i, points = (gsize) w * h;
    gint k;

    if (img)
        for (i = 0; i < points; i++)
            for (k = 0; k < 3; k++)
                img->px[i * 3 + k] = (rgba[i * 4 + k] * rgba[i * 4 + 3] + 255 * (255 - rgba[i * 4 + 3]) + 127) / 255;
    g_free(rgba);
    return img;
}

Image *
file_load_png_data(const guchar *data, gsize length, GError **error)
{
    gint w = 0, h = 0;
    guchar *rgba = gdk_png_load_from_data_straight(data, length, &w, &h, error);

    return from_rgba(rgba, w, h, error);
}

/* A picture from a file, which says itself what kind it is. */
Image *
file_load(const gchar *name, GError **error)
{
    gchar *data;
    gsize len;
    Image *img;

    if (!g_file_get_contents(name, &data, &len, error))
        return NULL;
    if (len >= 8 && !memcmp(data, "\211PNG\r\n\032\n", 8))
        img = file_load_png_data((const guchar *) data, len, error);
    else if (len >= 2 && !memcmp(data, "BM", 2))
        img = load_bmp((const guchar *) data, len, error);
    else if (len >= 9 && (!memcmp(data, "/* XPM */", 9) || strstr(data, "static char")))
        img = load_xpm(data, len, error);
    else {
        g_set_error(error, FILE_ERROR, 0, _("This is not a bitmap, an XPM or a PNG file."));
        img = NULL;
    }
    g_free(data);
    return img;
}

static void
png_put(png_structp png, png_bytep data, png_size_t n)
{
    g_byte_array_append(png_get_io_ptr(png), data, n);
}

static void
png_flush(png_structp png G_GNUC_UNUSED)
{
}

/* A picture as a PNG file would hold it, to be freed; NULL if libpng will not. */
guchar *
file_png_data(const Image *img, gsize *length)
{
    GByteArray *out = g_byte_array_new();
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png ? png_create_info_struct(png) : NULL;
    guchar *data;
    gint y;

    if (!info || setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        g_byte_array_free(out, TRUE);
        return NULL;
    }
    png_set_write_fn(png, out, png_put, png_flush);
    png_set_IHDR(png, info, img->w, img->h, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (y = 0; y < img->h; y++)
        png_write_row(png, img->px + (gsize) y * img->w * 3);
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    data = out->data;
    *length = out->len;
    g_byte_array_free(out, FALSE);
    return data;
}

/* The file is whole or is not written: it takes the place of the old one when done. */
gboolean
file_save(const Image *img, const gchar *name, gint format, GError **error)
{
    gboolean saved;

    if (format == FORMAT_PNG) {
        gsize len;
        guchar *data = file_png_data(img, &len);

        if (!data) {
            g_set_error(error, FILE_ERROR, 0, _("The picture could not be made into a PNG file."));
            return FALSE;
        }
        saved = g_file_set_contents(name, (const gchar *) data, len, error);
        g_free(data);
    } else if (format == FORMAT_XPM) {
        GString *out = xpm_data(img, name);

        saved = g_file_set_contents(name, out->str, out->len, error);
        g_string_free(out, TRUE);
    } else {
        GByteArray *out = bmp_data(img);

        saved = g_file_set_contents(name, (const gchar *) out->data, out->len, error);
        g_byte_array_free(out, TRUE);
    }
    return saved;
}
