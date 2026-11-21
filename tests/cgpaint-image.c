/* cgpaint-image.c - cgpaint's drawing and its files, with no window

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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cgpaint.h"

enum { WHITE = 0xffffff, RED = 0xff0000, BLUE = 0x0000ff };

static gint failed;

#define CHECK(what) \
    do { if (!(what)) { printf("line %d: %s\n", __LINE__, #what); failed++; } } while (0)

/* How many points of a picture are of a colour. */
static gint
count(const Image *img, Rgb rgb)
{
    gint x, y, n = 0;

    for (y = 0; y < img->h; y++)
        for (x = 0; x < img->w; x++)
            n += image_get(img, x, y) == rgb;
    return n;
}

static gboolean
same(const Image *a, const Image *b)
{
    return a && b && a->w == b->w && a->h == b->h && !memcmp(a->px, b->px, (gsize) a->w * a->h * 3);
}

/*
 * A shape's edge has no gaps if white poured in at its middle stays
 * within it: the corner of the picture is then as it was.
 */
static gboolean
tight(gint kind, gint w, gint h, gint width)
{
    Image *img = image_new(w + 20, h + 20, WHITE);
    gboolean held;

    image_shape(img, kind, 10, 10, 9 + w, 9 + h, width, FILL_NONE, RED, 0);
    image_flood(img, 10 + w / 2, 10 + h / 2, BLUE);
    held = image_get(img, 0, 0) == WHITE && image_get(img, 10 + w / 2, 10 + h / 2) == BLUE;
    image_free(img);
    return held;
}

/* Saved as a kind of file and read again, a picture is what it was. */
static gboolean
kept(const Image *img, const gchar *name)
{
    GError *error = NULL;
    Image *back;
    gboolean ok;

    if (!file_save(img, name, file_format(name), &error)) {
        printf("%s: %s\n", name, error->message);
        g_error_free(error);
        return FALSE;
    }
    if (!(back = file_load(name, &error))) {
        printf("%s: %s\n", name, error->message);
        g_error_free(error);
    }
    ok = same(img, back);
    image_free(back);
    unlink(name);
    return ok;
}

static Image *
loaded(const gchar *name, const gchar *data, gsize len)
{
    Image *img;

    g_file_set_contents(name, data, len, NULL);
    img = file_load(name, NULL);
    unlink(name);
    return img;
}

int
main(void)
{
    static const Pt tri[] = { { 10, 10 }, { 50, 10 }, { 10, 50 } };
    /* Two points by two of four colours, a bitmap of four bits a point, the bottom row first. */
    static const guchar bmp4[] = {
        'B', 'M', 0x4e, 0, 0, 0, 0, 0, 0, 0, 0x46, 0, 0, 0,
        40, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 1, 0, 4, 0, 0, 0, 0, 0,
        8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0xff, 0xff, 0xff, 0, 0, 0, 0xff, 0, 0xff, 0, 0, 0,
        0x23, 0, 0, 0, 0x01, 0, 0, 0
    };
    static const gchar xpm[] =
        "/* XPM */\nstatic char *t[] = {\n/* size */\n\"3 2 3 1\",\n"
        "\"  c None\",\n\". c red\",\n\"X\tc #00F\",\n\" .X\",\n\"X. \"\n};\n";
    Pen pen = { RED, PEN_ROUND, 1 };
    Image *img, *other, *mask;
    Rgb clear = WHITE;
    gint kind, w, width, x, y;
    guchar *data;
    gsize len;

    /* Lines and the pens. */
    img = image_new(64, 48, WHITE);
    CHECK(count(img, WHITE) == 64 * 48 && img->x1 <= img->x0);
    image_line(img, 2, 3, 12, 3, &pen);
    CHECK(count(img, RED) == 11 && img->x0 == 2 && img->x1 == 13 && img->y0 == 3 && img->y1 == 4);
    image_line(img, 5, 10, 25, 20, &pen);
    CHECK(count(img, RED) == 11 + 21);
    image_line(img, -30, -30, 100, 100, &pen);  /* off both ends, and no harm */
    image_fill(img, 0, 0, 64, 48, WHITE);
    pen.size = 3;
    image_dab(img, 20, 20, &pen);
    CHECK(count(img, RED) == 5 && image_get(img, 19, 19) == WHITE && image_get(img, 19, 20) == RED);
    pen.size = 5;
    image_dab(img, 40, 20, &pen);
    CHECK(count(img, RED) == 5 + 21);
    pen.shape = PEN_SQUARE;
    pen.size = 4;
    image_fill(img, 0, 0, 64, 48, WHITE);
    image_dab(img, 20, 20, &pen);
    CHECK(count(img, RED) == 16);

    /* Pouring: it stays within an edge, and goes round a corner. */
    image_fill(img, 0, 0, 64, 48, WHITE);
    image_shape(img, SHAPE_RECT, 10, 10, 30, 30, 1, FILL_NONE, RED, 0);
    CHECK(count(img, RED) == 80);
    image_flood(img, 20, 20, BLUE);
    CHECK(count(img, BLUE) == 19 * 19 && image_get(img, 0, 0) == WHITE);
    image_flood(img, 0, 0, BLUE);
    CHECK(count(img, WHITE) == 0 && count(img, RED) == 80);
    image_replace(img, 0, 0, 64, 20, BLUE, WHITE);
    CHECK(image_get(img, 0, 0) == WHITE && image_get(img, 0, 20) == BLUE && image_get(img, 10, 10) == RED);

    /* Shapes: an edge of the width asked for, with no gaps in it at any size. */
    image_fill(img, 0, 0, 64, 48, WHITE);
    image_shape(img, SHAPE_RECT, 10, 10, 30, 30, 3, FILL_BOTH, RED, BLUE);
    CHECK(image_get(img, 9, 9) == RED && image_get(img, 11, 11) == RED && image_get(img, 12, 12) == BLUE
          && image_get(img, 8, 8) == WHITE && count(img, BLUE) == 17 * 17);
    image_fill(img, 0, 0, 64, 48, WHITE);
    image_shape(img, SHAPE_ELLIPSE, 30, 30, 10, 10, 1, FILL_ONLY, RED, BLUE);
    CHECK(image_get(img, 20, 20) == RED && image_get(img, 10, 10) == WHITE && image_get(img, 10, 20) == RED
          && image_get(img, 30, 20) == RED && count(img, BLUE) == 0);
    for (kind = SHAPE_RECT; kind <= SHAPE_ROUND; kind++)
        for (width = 1; width <= 5; width++)
            for (w = 12; w < 90; w += 7)
                if (!tight(kind, w, 100 - w, width) || !tight(kind, w, w, width)) {
                    printf("a shape of kind %d, %d wide, edge %d, has a gap\n", kind, w, width);
                    failed++;
                }

    /* A triangle, filled: half of the square it is half of, near enough. */
    image_fill(img, 0, 0, 64, 48, WHITE);
    image_polygon(img, tri, 3, 1, FILL_BOTH, RED, BLUE);
    CHECK(image_get(img, 10, 10) == RED && image_get(img, 15, 15) == BLUE && image_get(img, 45, 45) == WHITE);
    CHECK(abs(count(img, RED) + count(img, BLUE) - 41 * 42 / 2) < 45);

    /* Turning: four quarters, or two flips each way, bring it back. */
    image_fill(img, 0, 0, 64, 48, WHITE);
    image_line(img, 3, 4, 50, 30, &pen);
    image_shape(img, SHAPE_ELLIPSE, 30, 5, 60, 40, 2, FILL_BOTH, BLUE, 0x00ff00);
    other = image_rotate(img, 1);
    CHECK(other->w == 48 && other->h == 64 && image_get(other, 47 - 4, 3) == image_get(img, 3, 4));
    for (x = 0; x < 3; x++) {
        Image *next = image_rotate(other, 1);

        image_free(other);
        other = next;
    }
    CHECK(same(img, other));
    image_flip(other, FALSE);
    CHECK(!same(img, other) && image_get(other, 63 - 3, 4) == image_get(img, 3, 4));
    image_flip(other, TRUE);
    image_free(other);
    other = image_rotate(img, 2);
    image_flip(other, TRUE);
    image_flip(other, FALSE);
    CHECK(same(img, other));
    image_invert(other);
    CHECK(image_get(other, 0, 0) == 0);
    image_invert(other);
    CHECK(same(img, other));
    image_free(other);

    /* Stretching to twice the size and back; slanting, which makes it wider. */
    other = image_scale(img, 128, 96);
    CHECK(image_get(other, 7, 9) == image_get(img, 3, 4));
    mask = image_scale(other, 64, 48);
    CHECK(same(img, mask));
    image_free(mask);
    image_free(other);
    other = image_skew(img, 45, 0, 0x123456);
    CHECK(other->w == 64 + 47 && other->h == 48 && image_get(other, 0, 47) == 0x123456
          && image_get(other, 47, 47) == image_get(img, 0, 47) && image_get(other, 0, 0) == image_get(img, 0, 0));
    image_free(other);
    CHECK(image_skew(img, 89.9, 89.9, 0) == NULL);

    /* A piece cut out and laid down again, through a mask, and with a colour let through. */
    other = image_crop(img, 60, 40, 10, 10, 0x123456);
    CHECK(other->w == 10 && image_get(other, 0, 0) == image_get(img, 60, 40) && image_get(other, 9, 9) == 0x123456);
    image_free(other);
    other = image_new(4, 4, RED);
    image_span(other, 0, 3, 0, WHITE);
    mask = image_new(4, 4, 0);
    image_fill(mask, 0, 0, 2, 4, WHITE);
    image_fill(img, 0, 0, 64, 48, BLUE);
    image_blend(img, other, mask, 62, 10, &clear);
    CHECK(image_get(img, 62, 10) == BLUE && image_get(img, 62, 11) == RED && image_get(img, 63, 13) == RED
          && count(img, RED) == 6);
    image_blend(img, other, NULL, -2, -2, NULL);
    CHECK(image_get(img, 0, 0) == RED && image_get(img, 1, 1) == RED && image_get(img, 2, 2) == BLUE);
    image_free(other);
    image_free(mask);

    /* Files: each kind keeps every point, of few colours and of very many. */
    CHECK(file_format("a.BMP") == FORMAT_BMP && file_format("b.d/a.xpm") == FORMAT_XPM
          && file_format("a.png") == FORMAT_PNG && file_format("b.png/a") == FORMAT_NONE);
    CHECK(kept(img, "cgpaint-image.tmp.bmp") && kept(img, "cgpaint-image.tmp.xpm") && kept(img, "cgpaint-image.tmp.png"));
    image_free(img);
    img = image_new(301, 97, WHITE);  /* an odd width, for the bitmap's rows */
    for (y = 0; y < img->h; y++)
        for (x = 0; x < img->w; x++)
            image_span(img, x, x, y, (Rgb) (x * 7919 + y * 104729) * 2654435761u >> 8);
    CHECK(kept(img, "cgpaint-image.tmp.bmp") && kept(img, "1 cgpaint-image.tmp.xpm") && kept(img, "cgpaint-image.tmp.png"));
    data = file_png_data(img, &len);
    other = data ? file_load_png_data(data, len, NULL) : NULL;
    CHECK(same(img, other));
    image_free(other);
    g_free(data);
    image_free(img);

    /* Files from elsewhere: a bitmap with a palette, an XPM with names and None. */
    img = loaded("cgpaint-image.tmp", (const gchar *) bmp4, sizeof bmp4);
    CHECK(img && img->w == 2 && img->h == 2 && image_get(img, 0, 0) == 0 && image_get(img, 1, 0) == WHITE
          && image_get(img, 0, 1) == RED && image_get(img, 1, 1) == BLUE);
    image_free(img);
    img = loaded("cgpaint-image.tmp", xpm, sizeof xpm - 1);
    CHECK(img && img->w == 3 && img->h == 2 && image_get(img, 0, 0) == WHITE && image_get(img, 1, 0) == RED
          && image_get(img, 2, 0) == BLUE && image_get(img, 0, 1) == BLUE && image_get(img, 2, 1) == WHITE);
    image_free(img);

    /* And what is no picture, or is cut short, is turned away and nothing more. */
    CHECK(!loaded("cgpaint-image.tmp", "hello", 5));
    CHECK(!loaded("cgpaint-image.tmp", (const gchar *) bmp4, sizeof bmp4 - 5));
    CHECK(!loaded("cgpaint-image.tmp", (const gchar *) bmp4, 30));
    CHECK(!loaded("cgpaint-image.tmp", xpm, sizeof xpm - 12));
    CHECK(!loaded("cgpaint-image.tmp", "\211PNG\r\n\032\n....", 12));

    if (failed)
        printf("%d failed\n", failed);
    return failed != 0;
}
