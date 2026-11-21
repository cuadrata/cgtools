/* image.c - a picture, and what is drawn on it

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

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cgpaint.h"

enum { CORNER = 8 };  /* the radius of a rounded rectangle's corners */

Image *
image_new(gint w, gint h, Rgb rgb)
{
    Image *img = g_new0(Image, 1);

    img->w = w;
    img->h = h;
    img->px = g_malloc((gsize) w * h * 3);
    image_fill(img, 0, 0, w, h, rgb);
    image_clean(img);
    return img;
}

Image *
image_copy(const Image *img)
{
    Image *copy = g_new0(Image, 1);

    copy->w = img->w;
    copy->h = img->h;
    copy->px = g_malloc((gsize) img->w * img->h * 3);
    memcpy(copy->px, img->px, (gsize) img->w * img->h * 3);
    return copy;
}

void
image_free(Image *img)
{
    if (img) {
        g_free(img->px);
        g_free(img);
    }
}

/* Nothing of it has been drawn on, from now. */
void
image_clean(Image *img)
{
    img->x0 = img->y0 = img->x1 = img->y1 = 0;
}

/* This much of it has been drawn on. */
void
image_touch(Image *img, gint x, gint y, gint w, gint h)
{
    gint x1 = MIN(x + w, img->w), y1 = MIN(y + h, img->h);

    x = MAX(x, 0);
    y = MAX(y, 0);
    if (x >= x1 || y >= y1)
        return;
    if (img->x1 <= img->x0) {
        img->x0 = x;
        img->y0 = y;
        img->x1 = x1;
        img->y1 = y1;
    } else {
        img->x0 = MIN(img->x0, x);
        img->y0 = MIN(img->y0, y);
        img->x1 = MAX(img->x1, x1);
        img->y1 = MAX(img->y1, y1);
    }
}

static guchar *
at(const Image *img, gint x, gint y)
{
    return img->px + ((gsize) y * img->w + x) * 3;
}

static Rgb
rgb_at(const guchar *p)
{
    return (Rgb) p[0] << 16 | p[1] << 8 | p[2];
}

/* The colour of a point, which is in the picture. */
Rgb
image_get(const Image *img, gint x, gint y)
{
    return rgb_at(at(img, x, y));
}

/* A row of points, from x0 to x1: all drawing comes to this. */
void
image_span(Image *img, gint x0, gint x1, gint y, Rgb rgb)
{
    guchar *p, r = rgb >> 16, g = rgb >> 8, b = rgb;

    x0 = MAX(x0, 0);
    x1 = MIN(x1, img->w - 1);
    if (y < 0 || y >= img->h || x0 > x1)
        return;
    image_touch(img, x0, y, x1 - x0 + 1, 1);
    for (p = at(img, x0, y); x0 <= x1; x0++, p += 3) {
        p[0] = r;
        p[1] = g;
        p[2] = b;
    }
}

void
image_fill(Image *img, gint x, gint y, gint w, gint h, Rgb rgb)
{
    gint y1 = MIN(y + h, img->h);

    for (y = MAX(y, 0); y < y1; y++)
        image_span(img, x, x + w - 1, y, rgb);
}

/*
 * The pen set down once, about a point. A round one is every point
 * whose middle is within its circle, drawn a little small so that one
 * of three across is a cross and not a square. A slanted one is two
 * points thick, so that going across its slant leaves no gaps. One
 * that draws only on a colour is a square, as it is the eraser's.
 */
void
image_dab(Image *img, gint x, gint y, const Pen *pen)
{
    gint d = pen->size, i, j, u, v;

    if (pen->only) {
        image_replace(img, x - d / 2, y - d / 2, d, d, *pen->only, pen->rgb);
        return;
    }
    if (d <= 1) {
        image_span(img, x, x, y, pen->rgb);
        return;
    }
    x -= d / 2;
    y -= d / 2;
    for (j = 0; j < d; j++)
        switch (pen->shape) {
        case PEN_SQUARE:
            image_span(img, x, x + d - 1, y + j, pen->rgb);
            break;
        case PEN_SLASH:
            image_span(img, x + d - 1 - j, x + d - j, y + j, pen->rgb);
            break;
        case PEN_BACKSLASH:
            image_span(img, x + j, x + j + 1, y + j, pen->rgb);
            break;
        default:
            v = 2 * j + 1 - d;
            for (i = 0; i < d; i++) {
                u = 2 * i + 1 - d;
                if (u * u + v * v <= d * d - 2)
                    break;
            }
            image_span(img, x + i, x + d - 1 - i, y + j, pen->rgb);
            break;
        }
}

void
image_line(Image *img, gint x0, gint y0, gint x1, gint y1, const Pen *pen)
{
    gint dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    gint err = dx + dy, e2;

    for (;;) {
        image_dab(img, x0, y0, pen);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* A curve from p[0] to p[3] that is drawn towards p[1] and then p[2]. */
void
image_curve(Image *img, const Pt p[4], const Pen *pen)
{
    gint n = 0, i, x = p[0].x, y = p[0].y, nx, ny;
    gdouble t, u;

    for (i = 0; i < 3; i++)
        n += abs(p[i + 1].x - p[i].x) + abs(p[i + 1].y - p[i].y);
    n = CLAMP(n / 4, 8, 500);
    for (i = 1; i <= n; i++) {
        t = (gdouble) i / n;
        u = 1 - t;
        nx = floor(u * u * u * p[0].x + 3 * u * u * t * p[1].x + 3 * u * t * t * p[2].x + t * t * t * p[3].x + 0.5);
        ny = floor(u * u * u * p[0].y + 3 * u * u * t * p[1].y + 3 * u * t * t * p[2].y + t * t * t * p[3].y + 0.5);
        image_line(img, x, y, nx, ny, pen);
        x = nx;
        y = ny;
    }
}

/* Some points at random within a circle. */
void
image_spray(Image *img, gint x, gint y, gint radius, gint dots, Rgb rgb)
{
    gint dx, dy;

    while (dots-- > 0) {
        do {
            dx = rand() % (2 * radius + 1) - radius;
            dy = rand() % (2 * radius + 1) - radius;
        } while (dx * dx + dy * dy > radius * radius);
        image_span(img, x + dx, x + dx, y + dy, rgb);
    }
}

/*
 * Every point of one colour that can be reached from this one, going
 * up, down and across, takes the new colour. A row is filled at a
 * time, and the rows above and below it are looked over for where to
 * go on from.
 */
void
image_flood(Image *img, gint x, gint y, Rgb rgb)
{
    GArray *todo;
    Rgb old;
    gint x0, x1, i, dy;

    if (x < 0 || y < 0 || x >= img->w || y >= img->h || (old = image_get(img, x, y)) == rgb)
        return;
    todo = g_array_new(FALSE, FALSE, sizeof(gint));
    g_array_append_val(todo, x);
    g_array_append_val(todo, y);
    while (todo->len) {
        y = g_array_index(todo, gint, todo->len - 1);
        x = g_array_index(todo, gint, todo->len - 2);
        g_array_set_size(todo, todo->len - 2);
        if (image_get(img, x, y) != old)
            continue;
        for (x0 = x; x0 > 0 && image_get(img, x0 - 1, y) == old; x0--)
            ;
        for (x1 = x; x1 < img->w - 1 && image_get(img, x1 + 1, y) == old; x1++)
            ;
        image_span(img, x0, x1, y, rgb);
        for (dy = -1; dy <= 1; dy += 2) {
            gint ny = y + dy;

            if (ny < 0 || ny >= img->h)
                continue;
            for (i = x0; i <= x1; i++)
                if (image_get(img, i, ny) == old && (i == x0 || image_get(img, i - 1, ny) != old)) {
                    g_array_append_val(todo, i);
                    g_array_append_val(todo, ny);
                }
        }
    }
    g_array_free(todo, TRUE);
}

/* Within a rectangle, what is of one colour takes another. */
void
image_replace(Image *img, gint x, gint y, gint w, gint h, Rgb from, Rgb to)
{
    gint x0 = MAX(x, 0), x1 = MIN(x + w, img->w), y1 = MIN(y + h, img->h), i;

    for (y = MAX(y, 0); y < y1; y++)
        for (i = x0; i < x1; i++)
            if (image_get(img, i, y) == from)
                image_span(img, i, i, y, to);
}

/*
 * Where a shape that fills w by h points lies in one of their rows:
 * from *a to *b, and FALSE if nowhere. A point is in the shape when
 * its middle is. r is the radius of a rounded rectangle's corners.
 */
static gboolean
shape_row(gint kind, gint w, gint h, gint r, gint row, gint *a, gint *b)
{
    gdouble dy, in;

    if (row < 0 || row >= h || w <= 0)
        return FALSE;
    *a = 0;
    if (kind == SHAPE_ELLIPSE) {
        dy = (row + 0.5 - h / 2.0) / (h / 2.0);
        in = w / 2.0 * (1 - sqrt(MAX(1 - dy * dy, 0)));
        *a = ceil(in - 0.5 - 1e-9);
    } else if (kind == SHAPE_ROUND && r > 0) {
        dy = MAX(r - (row + 0.5), row + 0.5 - (h - r));
        if (dy > 0)
            *a = ceil(r - sqrt(MAX(r * r - dy * dy, 0)) - 0.5 - 1e-9);
    }
    *b = w - 1 - *a;
    return *a <= *b;
}

/*
 * A rectangle, an ellipse or a rounded rectangle between two corners,
 * with an edge of some width that goes as far outside the corners as
 * inside them. Row by row: the edge is what of the shape is not in
 * the same shape drawn smaller within it, nor has the shape on every
 * side of it, so that a thin edge has no gaps where it is steep.
 */
void
image_shape(Image *img, gint kind, gint x0, gint y0, gint x1, gint y1,
            gint width, gint style, Rgb line, Rgb fill)
{
    gint x = MIN(x0, x1), y = MIN(y0, y1), w = abs(x1 - x0) + 1, h = abs(y1 - y0) + 1;
    gint out = style == FILL_ONLY ? 0 : width / 2, r, ri, row, a, b, c, d, e, f;

    x -= out;
    y -= out;
    w += 2 * out;
    h += 2 * out;
    r = MIN(CORNER + out, MIN(w, h) / 2);
    ri = MIN(MAX(r - width, 0), MIN(w, h) / 2 - width);
    for (row = MAX(-y, 0); row < h && y + row < img->h; row++) {
        if (!shape_row(kind, w, h, r, row, &a, &b))
            continue;
        if (style == FILL_ONLY) {
            image_span(img, x + a, x + b, y + row, line);
            continue;
        }
        if (shape_row(kind, w - 2 * width, h - 2 * width, ri, row - width, &c, &d)) {
            c = MAX(c + width, a + 1);
            d = MIN(d + width, b - 1);
        } else
            c = b + 1, d = a - 1;
        if (shape_row(kind, w, h, r, row - 1, &e, &f)) {
            c = MAX(c, e);
            d = MIN(d, f);
        } else
            c = b + 1, d = a - 1;
        if (shape_row(kind, w, h, r, row + 1, &e, &f)) {
            c = MAX(c, e);
            d = MIN(d, f);
        } else
            c = b + 1, d = a - 1;
        if (c > d) {
            image_span(img, x + a, x + b, y + row, line);
            continue;
        }
        image_span(img, x + a, x + c - 1, y + row, line);
        image_span(img, x + d + 1, x + b, y + row, line);
        if (style == FILL_BOTH)
            image_span(img, x + c, x + d, y + row, fill);
    }
}

static gint
by_x(gconstpointer a, gconstpointer b)
{
    gdouble x = *(const gdouble *) a, y = *(const gdouble *) b;

    return x < y ? -1 : x > y;
}

/*
 * A shape of straight sides through some points, the last joined to
 * the first. Its inside is what an odd number of its sides lie to the
 * left of, as it was in Paint.
 */
void
image_polygon(Image *img, const Pt *pts, gint n, gint width, gint style, Rgb line, Rgb fill)
{
    Pen pen = { line, PEN_ROUND, style == FILL_ONLY ? 1 : width, NULL };
    gdouble *xs;
    gint y, y0, y1, i, k;

    if (n < 1)
        return;
    if (style != FILL_NONE && n > 2) {
        y0 = y1 = pts[0].y;
        for (i = 1; i < n; i++) {
            y0 = MIN(y0, pts[i].y);
            y1 = MAX(y1, pts[i].y);
        }
        xs = g_new(gdouble, n);
        for (y = MAX(y0, 0); y <= y1 && y < img->h; y++) {
            for (i = k = 0; i < n; i++) {
                const Pt *p = &pts[i], *q = &pts[(i + 1) % n];

                if ((p->y <= y) != (q->y <= y))
                    xs[k++] = p->x + (gdouble) (y - p->y) * (q->x - p->x) / (q->y - p->y);
            }
            qsort(xs, k, sizeof *xs, by_x);
            for (i = 0; i + 1 < k; i += 2)
                image_span(img, ceil(xs[i] - 1e-9), floor(xs[i + 1] + 1e-9), y,
                           style == FILL_ONLY ? line : fill);
        }
        g_free(xs);
    }
    for (i = 0; i < n; i++)
        image_line(img, pts[i].x, pts[i].y, pts[(i + 1) % n].x, pts[(i + 1) % n].y, &pen);
}

/* Part of a picture is made as it is in another of the same size. */
void
image_restore(Image *img, const Image *from, gint x, gint y, gint w, gint h)
{
    gint x1 = MIN(x + w, img->w), y1 = MIN(y + h, img->h);

    x = MAX(x, 0);
    y = MAX(y, 0);
    if (x >= x1 || y >= y1)
        return;
    image_touch(img, x, y, x1 - x, y1 - y);
    for (; y < y1; y++)
        memcpy(at(img, x, y), at(from, x, y), (gsize) (x1 - x) * 3);
}

/*
 * A picture laid on another, its top left at x, y. A mask of its size
 * says how much of each point is laid, by its red, and nothing of the
 * colour *clear is laid at all.
 */
void
image_blend(Image *img, const Image *src, const Image *mask, gint x, gint y, const Rgb *clear)
{
    gint i0 = MAX(-x, 0), i1 = MIN(src->w, img->w - x), j0 = MAX(-y, 0), j1 = MIN(src->h, img->h - y);
    gint i, j, k, m;

    if (i0 >= i1 || j0 >= j1)
        return;
    image_touch(img, x + i0, y + j0, i1 - i0, j1 - j0);
    for (j = j0; j < j1; j++) {
        const guchar *s = at(src, i0, j), *a = mask ? at(mask, i0, j) : NULL;
        guchar *p = at(img, x + i0, y + j);

        for (i = i0; i < i1; i++, s += 3, p += 3) {
            m = a ? a[(i - i0) * 3] : 255;
            if (m == 0 || (clear && rgb_at(s) == *clear))
                continue;
            for (k = 0; k < 3; k++)
                p[k] = m == 255 ? s[k] : (s[k] * m + p[k] * (255 - m) + 127) / 255;
        }
    }
}

/* A rectangle of a picture as a picture; what of it lies outside is of a colour. */
Image *
image_crop(const Image *img, gint x, gint y, gint w, gint h, Rgb back)
{
    Image *crop = image_new(w, h, back);
    gint i0 = MAX(-x, 0), i1 = MIN(w, img->w - x), j0 = MAX(-y, 0), j1 = MIN(h, img->h - y), j;

    if (i0 < i1)
        for (j = j0; j < j1; j++)
            memcpy(at(crop, i0, j), at(img, x + i0, y + j), (gsize) (i1 - i0) * 3);
    return crop;
}

/* Turned over, left to right or top to bottom. */
void
image_flip(Image *img, gboolean vertical)
{
    gint x, y, k;
    guchar *p, *q, t;

    for (y = 0; y < (vertical ? img->h / 2 : img->h); y++)
        for (x = 0; x < (vertical ? img->w : img->w / 2); x++) {
            p = at(img, x, y);
            q = vertical ? at(img, x, img->h - 1 - y) : at(img, img->w - 1 - x, y);
            for (k = 0; k < 3; k++) {
                t = p[k];
                p[k] = q[k];
                q[k] = t;
            }
        }
    image_touch(img, 0, 0, img->w, img->h);
}

void
image_invert(Image *img)
{
    gsize i, n = (gsize) img->w * img->h * 3;

    for (i = 0; i < n; i++)
        img->px[i] = ~img->px[i];
    image_touch(img, 0, 0, img->w, img->h);
}

/* Turned by so many quarters of a circle, as a clock's hand goes. */
Image *
image_rotate(const Image *img, gint quarters)
{
    gboolean side = quarters & 1;
    Image *turned = image_new(side ? img->h : img->w, side ? img->w : img->h, 0);
    gint x, y;

    for (y = 0; y < turned->h; y++)
        for (x = 0; x < turned->w; x++) {
            const guchar *p;

            switch (quarters & 3) {
            case 1: p = at(img, y, img->h - 1 - x); break;
            case 2: p = at(img, img->w - 1 - x, img->h - 1 - y); break;
            case 3: p = at(img, img->w - 1 - y, x); break;
            default: p = at(img, x, y); break;
            }
            memcpy(at(turned, x, y), p, 3);
        }
    return turned;
}

/* Stretched or squeezed to a size, each point taken from the nearest. */
Image *
image_scale(const Image *img, gint w, gint h)
{
    Image *scaled = image_new(w, h, 0);
    gint x, y;

    for (y = 0; y < h; y++) {
        const guchar *row = at(img, 0, (gint) ((gint64) y * img->h / h));
        guchar *p = at(scaled, 0, y);

        for (x = 0; x < w; x++, p += 3)
            memcpy(p, row + (gsize) ((gint64) x * img->w / w) * 3, 3);
    }
    return scaled;
}

/* How large a picture is once slanted; FALSE if larger than a picture may be. */
gboolean
image_skew_size(gint w, gint h, gdouble across, gdouble down, gint *sw, gint *sh)
{
    const gdouble degree = 3.14159265358979323846 / 180;
    gdouble wide = w + ceil(fabs(tan(across * degree)) * (h - 1));
    gdouble high = h + ceil(fabs(tan(down * degree)) * (wide - 1));

    if (wide > IMAGE_MAX || high > IMAGE_MAX)
        return FALSE;
    *sw = wide;
    *sh = high;
    return TRUE;
}

/*
 * Slanted: by so many degrees across, each row going further right
 * than the one above it, or further left if the angle is below
 * nothing; and then as many down, column by column, in the same way.
 * What is left bare is of a colour. NULL if it would be too large.
 */
Image *
image_skew(const Image *img, gdouble across, gdouble down, Rgb back)
{
    const gdouble degree = 3.14159265358979323846 / 180;
    gdouble ta = tan(across * degree), td = tan(down * degree);
    Image *first, *skewed;
    gint w, h, x, y, by;

    if (!image_skew_size(img->w, img->h, across, down, &w, &h))
        return NULL;
    first = image_new(w, img->h, back);
    for (y = 0; y < img->h; y++)
        memcpy(at(first, floor((ta < 0 ? img->h - 1 - y : y) * fabs(ta) + 0.5), y),
               at(img, 0, y), (gsize) img->w * 3);
    skewed = image_new(w, h, back);
    for (x = 0; x < w; x++) {
        by = floor((td < 0 ? w - 1 - x : x) * fabs(td) + 0.5);
        for (y = 0; y < img->h; y++)
            memcpy(at(skewed, x, by + y), at(first, x, y), 3);
    }
    image_free(first);
    return skewed;
}
