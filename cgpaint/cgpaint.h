/* cgpaint.h - what the parts of cgpaint share

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

#ifndef CGPAINT_H
#define CGPAINT_H

#include <gtk/gtk.h>

#ifdef ENABLE_NLS
#include <libintl.h>
#define _(String) gettext(String)
#else
#define _(String) (String)
#define textdomain(String) (String)
#define bindtextdomain(Domain,Directory) (Domain)
#endif
#define N_(String) (String)

enum { IMAGE_MAX = 16384 };  /* a picture is no wider or higher */

typedef guint32 Rgb;  /* 0xRRGGBB */

/*
 * A picture: red, green and blue, a byte each, in rows from the top.
 * x0 to y1 hold what of it has been drawn on since image_clean(), for
 * the window to show again; nothing if x1 is not beyond x0.
 */
typedef struct {
    gint w, h;
    guchar *px;
    gint x0, y0, x1, y1;
} Image;

typedef struct {
    gint x, y;
} Pt;

/* What draws a line: a shape of a size, in a colour, set down all along it. */
enum { PEN_ROUND, PEN_SQUARE, PEN_SLASH, PEN_BACKSLASH };

typedef struct {
    Rgb rgb;
    gint shape, size;
    const Rgb *only;  /* if there is one, only what is of that colour is drawn on */
} Pen;

enum { SHAPE_RECT, SHAPE_ELLIPSE, SHAPE_ROUND };

/* A closed shape is its edge alone, its edge and its inside, or all of one colour. */
enum { FILL_NONE, FILL_BOTH, FILL_ONLY };

enum { FORMAT_NONE, FORMAT_BMP, FORMAT_XPM, FORMAT_PNG };

#define FILE_ERROR g_quark_from_static_string("cgpaint-file-error")

/* image.c */
Image *image_new(gint w, gint h, Rgb rgb);
Image *image_copy(const Image *img);
void image_free(Image *img);
void image_clean(Image *img);
void image_touch(Image *img, gint x, gint y, gint w, gint h);
Rgb image_get(const Image *img, gint x, gint y);
void image_span(Image *img, gint x0, gint x1, gint y, Rgb rgb);
void image_fill(Image *img, gint x, gint y, gint w, gint h, Rgb rgb);
void image_dab(Image *img, gint x, gint y, const Pen *pen);
void image_line(Image *img, gint x0, gint y0, gint x1, gint y1, const Pen *pen);
void image_curve(Image *img, const Pt p[4], const Pen *pen);
void image_spray(Image *img, gint x, gint y, gint radius, gint dots, Rgb rgb);
void image_flood(Image *img, gint x, gint y, Rgb rgb);
void image_replace(Image *img, gint x, gint y, gint w, gint h, Rgb from, Rgb to);
void image_shape(Image *img, gint kind, gint x0, gint y0, gint x1, gint y1,
                 gint width, gint style, Rgb line, Rgb fill);
void image_polygon(Image *img, const Pt *pts, gint n, gint width, gint style, Rgb line, Rgb fill);
void image_restore(Image *img, const Image *from, gint x, gint y, gint w, gint h);
void image_blend(Image *img, const Image *src, const Image *mask, gint x, gint y, const Rgb *clear);
Image *image_crop(const Image *img, gint x, gint y, gint w, gint h, Rgb back);
void image_flip(Image *img, gboolean vertical);
void image_invert(Image *img);
Image *image_rotate(const Image *img, gint quarters);
Image *image_scale(const Image *img, gint w, gint h);
gboolean image_skew_size(gint w, gint h, gdouble across, gdouble down, gint *sw, gint *sh);
Image *image_skew(const Image *img, gdouble across, gdouble down, Rgb back);

/* file.c */
gint file_format(const gchar *name);
Image *file_load(const gchar *name, GError **error);
Image *file_load_png_data(const guchar *data, gsize length, GError **error);
gboolean file_save(const Image *img, const gchar *name, gint format, GError **error);
guchar *file_png_data(const Image *img, gsize *length);

/* The tools, as the tool box has them, two to a row. */
enum {
    TOOL_FREE, TOOL_SELECT, TOOL_ERASER, TOOL_FILL, TOOL_PICK, TOOL_ZOOM, TOOL_PENCIL, TOOL_BRUSH,
    TOOL_AIRBRUSH, TOOL_TEXT, TOOL_LINE, TOOL_CURVE, TOOL_RECT, TOOL_POLYGON, TOOL_ELLIPSE, TOOL_ROUND,
    TOOLS
};

/* A brush: a pen's shape and its size. */
typedef struct {
    gint shape, size;
} Brush;

/*
 * view.c: the picture and what the user has chosen to draw it with.
 * What a tool can be told is kept as which of its choices it is, the
 * first being nought, as the box beneath the tools shows them.
 */
extern Image *image;
extern gboolean modified;   /* the picture is not as its file has it */
extern gint tool, zoom;
extern Rgb fg, bg;          /* the colours of the left button and of the right */
extern gint see_through;    /* what is selected, and text, is laid down without its background */
extern gint eraser, brush, spray, thick, filling;
extern gint magnify;        /* which of the magnifier's sizes the picture is at, or -1 */
extern gboolean grid;
extern const Brush brushes[12];
extern const gint magnified[4];

GtkWidget *view_new(void);
void view_font(GdkFont *font);
void view_open(Image *img);
void view_finish(void);
void view_tool(gint which);
void view_options(void);
void view_zoom(gint times);
gboolean view_undo(void);
gboolean view_redo(void);
void view_copy(void);
void view_cut(void);
void view_paste(void);
void view_paste_image(Image *img);
void view_delete(void);
void view_select_all(void);
Image *view_selected(void);
void view_flip(gboolean vertical);
void view_rotate(gint quarters);
gboolean view_stretch(gdouble wide, gdouble high, gdouble across, gdouble downward);
void view_invert(void);
void view_resize(gint w, gint h);
void view_clear(void);

/* cgpaint.c: what the window is told by the picture */
void ui_position(gint x, gint y);
void ui_size(gint w, gint h);
void ui_chosen(void);
void ui_tool(gint which);

#endif /* CGPAINT_H */
