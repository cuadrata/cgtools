/* view.c - the picture in its window, and the tools at work on it

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

#include <stdlib.h>
#include <string.h>

#include <gdk/gdkkeysyms.h>

#include "cgpaint.h"

enum {
    MARGIN = 4,              /* bare work space about the picture */
    GRIP = 5,                /* how large a handle is */
    NEAR = 3,                /* how close is on a handle, or at a polygon's start */
    SPRAY = 30,              /* milliseconds between an airbrush's puffs */
    UNDO_BYTES = 128 << 20,  /* how much memory the earlier pictures may take */
    FAR = 4 * IMAGE_MAX      /* further from the picture than this is no further */
};

/* What the pointer is doing while a button is down. */
enum { DO_NOTHING, DO_STROKE, DO_BAND, DO_PICK, DO_MARQUEE, DO_LASSO, DO_MOVE, DO_RESIZE };

Image *image;
gboolean modified;
gint tool = TOOL_PENCIL, zoom = 1;
Rgb fg = 0x000000, bg = 0xffffff;
gint see_through, eraser = 2, magnify, brush = 1, spray, thick, filling;
gboolean grid;

const Brush brushes[12] = {
    { PEN_ROUND, 7 }, { PEN_ROUND, 4 }, { PEN_ROUND, 1 },
    { PEN_SQUARE, 8 }, { PEN_SQUARE, 5 }, { PEN_SQUARE, 2 },
    { PEN_SLASH, 8 }, { PEN_SLASH, 5 }, { PEN_SLASH, 2 },
    { PEN_BACKSLASH, 8 }, { PEN_BACKSLASH, 5 }, { PEN_BACKSLASH, 2 }
};
const gint magnified[4] = { 1, 2, 6, 8 };

/*
 * What is selected: a rectangle of the picture, all of it or what a
 * mask of its size lets through. It is in the picture until it is
 * first moved or changed; then it is lifted, a picture of its own
 * that lies over the other and is laid down in it when the selection
 * ends. What was pasted, and text as it is typed, are the same thing
 * and were never in the picture.
 */
static struct {
    gboolean on;
    gint x, y, w, h;
    Image *bits, *mask;
    gboolean pushed;  /* the picture as it was before this is among the undos */
    gboolean text;
    Pt hold;          /* where in it the pointer has hold of it */
} sel;

static GtkWidget *view;
static GtkAdjustment *hadj, *vadj;
static GList *undos, *redos;  /* the picture as it was, the latest first, and as it was before undoing */
static Image *before;         /* the picture as it was when a tool set to work on it */
static gint drawn[4];         /* what of it the tool has changed and may yet change back */
static gint doing, button, stage, grip, back_to;
static Pt down, last;         /* where the button went down, and where the pointer was last */
static Rgb ink, paper;        /* the colours of the button that is down, and of the other */
static GArray *pts;           /* a polygon's corners, or the way round a free selection */
static Pt bend[4];            /* a curve: its ends, and between them what it bends towards */
static guint puffing;
static gint new_w, new_h;     /* the picture's size as a handle is dragged */
static Image *clip_bits, *clip_mask;  /* what was copied, while the clipboard is ours */
static GdkAtom clipboard, png;
static GString *typed;        /* the text being typed */
static GdkFont *font;
static gint text_h, caret_x, caret_y;

static void text_render(void);

static gint
scroll_x(void)
{
    return hadj ? (gint) hadj->value : 0;
}

static gint
scroll_y(void)
{
    return vadj ? (gint) vadj->value : 0;
}

/* Which point of the picture, or of the plane it lies in, is at a point of the window. */
static void
to_picture(gdouble wx, gdouble wy, gint *x, gint *y)
{
    gint vx = (gint) wx + scroll_x() - MARGIN, vy = (gint) wy + scroll_y() - MARGIN;

    *x = CLAMP(vx >= 0 ? vx / zoom : -((zoom - 1 - vx) / zoom), -FAR, FAR);
    *y = CLAMP(vy >= 0 ? vy / zoom : -((zoom - 1 - vy) / zoom), -FAR, FAR);
}

/* Part of the picture, and what lies a point about it, is to be drawn again. */
static void
show(gint x, gint y, gint w, gint h)
{
    gint x0 = (x - 1) * zoom + MARGIN - scroll_x(), y0 = (y - 1) * zoom + MARGIN - scroll_y();
    gint x1 = x0 + (w + 2) * zoom, y1 = y0 + (h + 2) * zoom;

    x0 = MAX(x0, 0);
    y0 = MAX(y0, 0);
    x1 = MIN(x1, view->allocation.width);
    y1 = MIN(y1, view->allocation.height);
    if (x0 < x1 && y0 < y1)
        gtk_widget_queue_draw_area(view, x0, y0, x1 - x0, y1 - y0);
}

/* What has been drawn on is shown. */
static void
flush(void)
{
    if (image->x1 > image->x0)
        show(image->x0, image->y0, image->x1 - image->x0, image->y1 - image->y0);
    image_clean(image);
}

static void
sel_show(void)
{
    if (sel.on)
        show(sel.x, sel.y, sel.w, sel.h);
}

static void
adjust(GtkAdjustment *adj, gint size, gint page)
{
    gfloat value;

    if (!adj)
        return;
    value = CLAMP(adj->value, 0, MAX(size - page, 0));
    adj->lower = 0;
    adj->upper = size;
    adj->page_size = page;
    adj->page_increment = MAX(page - 16, 16);
    adj->step_increment = 16;
    gtk_adjustment_changed(adj);
    if (value != adj->value)
        gtk_adjustment_set_value(adj, value);
}

/* The picture, the window or the magnification is of another size: the scroll bars follow. */
static void
sized(void)
{
    adjust(hadj, image->w * zoom + 2 * MARGIN + GRIP, view->allocation.width);
    adjust(vadj, image->h * zoom + 2 * MARGIN + GRIP, view->allocation.height);
    gtk_widget_queue_draw(view);
}

static void
pointer(GdkCursorType type)
{
    static GdkCursor *made[GDK_LAST_CURSOR];
    static gint now = -1;

    if ((gint) type == now || !view->window)
        return;
    if (!made[type])
        made[type] = gdk_cursor_new(type);
    gdk_window_set_cursor(view->window, made[type]);
    now = type;
}

static void
tool_pointer(void)
{
    pointer(tool == TOOL_PENCIL || tool == TOOL_BRUSH ? GDK_PENCIL
            : tool == TOOL_AIRBRUSH ? GDK_SPRAYCAN
            : tool == TOOL_TEXT ? GDK_XTERM : GDK_CROSSHAIR);
}

static void
free_images(GList **list)
{
    GList *l;

    for (l = *list; l; l = l->next)
        image_free(l->data);
    g_list_free(*list);
    *list = NULL;
}

/* The first of a list is taken from it. */
static Image *
pop(GList **list)
{
    GList *l = *list;
    Image *img = l->data;

    *list = g_list_remove_link(l, l);
    g_list_free_1(l);
    return img;
}

/*
 * The picture as it was, which is taken, is the first thing to go
 * back to. As many are kept as fit in the memory allowed them: three
 * at the least, as Paint kept, and fifty at the most.
 */
static void
undo_push(Image *was)
{
    guint keep = CLAMP(UNDO_BYTES / ((gsize) was->w * was->h * 3), 3, 50);
    GList *l;

    undos = g_list_prepend(undos, was);
    free_images(&redos);
    while (g_list_length(undos) > keep) {
        l = g_list_last(undos);
        image_free(pop(&l));
    }
    modified = TRUE;
}

/* Another picture takes the place of this one. */
static void
replace(Image *img)
{
    image_free(image);
    image = img;
    sized();
}

/* A tool sets to work: the picture is kept as it is, to go back to. */
static void
stroke_begin(void)
{
    before = image_copy(image);
    drawn[0] = drawn[1] = drawn[2] = drawn[3] = 0;
    image_clean(image);
}

static void
stroke_end(void)
{
    if (before)
        undo_push(before);
    before = NULL;
    flush();
}

static void
stroke_cancel(void)
{
    if (before) {
        image_free(image);
        image = before;
        before = NULL;
        gtk_widget_queue_draw(view);
    }
}

/* What the tool has drawn since it set to work is taken away again. */
static void
rubber(void)
{
    if (drawn[2] > drawn[0])
        image_restore(image, before, drawn[0], drawn[1], drawn[2] - drawn[0], drawn[3] - drawn[1]);
    flush();
}

/* And what it has drawn now is remembered, to be taken away in its turn. */
static void
keep(void)
{
    drawn[0] = image->x0;
    drawn[1] = image->y0;
    drawn[2] = image->x1;
    drawn[3] = image->y1;
    flush();
}

static const Rgb *
clear(void)
{
    return see_through && !sel.text ? &bg : NULL;
}

static void
sel_free(void)
{
    sel_show();
    image_free(sel.bits);
    image_free(sel.mask);
    memset(&sel, 0, sizeof sel);
}

/* The selection ends: what was lifted is laid down where it is. */
static void
sel_drop(void)
{
    if (sel.on && sel.bits && !(sel.text && !typed->len)) {
        if (!sel.pushed)
            undo_push(image_copy(image));
        image_blend(image, sel.bits, sel.mask, sel.x, sel.y, clear());
    }
    sel_free();
    flush();
}

/*
 * What is selected is lifted out of the picture, leaving the
 * background's colour where it was; or, as a copy, leaving itself
 * there, which what is lifted already does by being laid down.
 */
static void
lift(gboolean copy)
{
    Image *hole;

    if (!sel.pushed && (!sel.bits || copy)) {
        undo_push(image_copy(image));
        sel.pushed = TRUE;
    }
    if (sel.bits) {
        if (copy)
            image_blend(image, sel.bits, sel.mask, sel.x, sel.y, clear());
    } else {
        sel.bits = image_crop(image, sel.x, sel.y, sel.w, sel.h, bg);
        if (!copy) {
            hole = image_new(sel.w, sel.h, bg);
            image_blend(image, hole, sel.mask, sel.x, sel.y, NULL);
            image_free(hole);
        }
    }
    flush();
}

/* Something is set afloat on the picture, at the top left of what the window shows. */
static void
afloat(Image *bits, Image *mask)
{
    ui_tool(TOOL_SELECT);
    view_finish();
    sel.on = TRUE;
    sel.x = scroll_x() / zoom;
    sel.y = scroll_y() / zoom;
    sel.w = bits->w;
    sel.h = bits->h;
    sel.bits = bits;
    sel.mask = mask;
    sel_show();
}

/* A polygon is closed, and filled as its kind is. */
static void
polygon_close(void)
{
    rubber();
    image_polygon(image, (const Pt *) pts->data, pts->len, thick + 1, filling, ink, paper);
    stroke_end();
    stage = 0;
    doing = DO_NOTHING;
}

/* The free selection's way round is closed: the selection is what lies within. */
static void
lasso_close(void)
{
    Pt *p = (Pt *) pts->data;
    gint x0, y0, x1, y1;
    guint i;

    show(0, 0, image->w, image->h);
    if (pts->len < 3)
        return;
    x0 = x1 = p[0].x;
    y0 = y1 = p[0].y;
    for (i = 1; i < pts->len; i++) {
        x0 = MIN(x0, p[i].x);
        y0 = MIN(y0, p[i].y);
        x1 = MAX(x1, p[i].x);
        y1 = MAX(y1, p[i].y);
    }
    for (i = 0; i < pts->len; i++) {
        p[i].x -= x0;
        p[i].y -= y0;
    }
    memset(&sel, 0, sizeof sel);
    sel.on = TRUE;
    sel.x = x0;
    sel.y = y0;
    sel.w = x1 - x0 + 1;
    sel.h = y1 - y0 + 1;
    sel.mask = image_new(sel.w, sel.h, 0);
    image_polygon(sel.mask, p, pts->len, 1, FILL_ONLY, 0xffffff, 0);
}

/* Whatever is half done is done with, as it stands. */
void
view_finish(void)
{
    if (puffing)
        gtk_timeout_remove(puffing);
    puffing = 0;
    if (stage && tool == TOOL_POLYGON)
        polygon_close();
    else if (before)
        stroke_end();
    if (doing == DO_MARQUEE && !sel.text)
        sel.on = FALSE;
    stage = 0;
    doing = DO_NOTHING;
    button = 0;
    sel_drop();
    ui_size(0, 0);
}

/* What is half done is undone: the other button went down, or Escape. */
static void
cancel(void)
{
    if (puffing)
        gtk_timeout_remove(puffing);
    puffing = 0;
    stroke_cancel();
    if (doing == DO_MARQUEE || doing == DO_LASSO)
        sel.on = FALSE;
    stage = 0;
    doing = DO_NOTHING;
    button = 0;
    ui_size(0, 0);
    gtk_widget_queue_draw(view);
}

void
view_tool(gint which)
{
    view_finish();
    if (which == TOOL_PICK && tool != TOOL_PICK)
        back_to = tool;  /* a colour is picked, and then it is this tool's turn again */
    tool = which;
    tool_pointer();
}

/* A colour or a choice of the tool's is another: what shows it is drawn again. */
void
view_options(void)
{
    if (sel.on && sel.text)
        text_render();
    sel_show();
    if (grid || zoom > 1)
        gtk_widget_queue_draw(view);
}

/* A new picture, which is taken: the old one and all that was done to it are gone. */
void
view_open(Image *img)
{
    cancel();
    sel_free();
    free_images(&undos);
    free_images(&redos);
    replace(img);
    modified = FALSE;
}

static void
zoom_about(gint times, gint x, gint y)
{
    zoom = times;
    for (magnify = 3; magnify >= 0 && magnified[magnify] != zoom; magnify--)
        ;
    ui_chosen();
    sized();
    if (hadj)
        gtk_adjustment_set_value(hadj, CLAMP(x * zoom + MARGIN - view->allocation.width / 2, 0,
                                             MAX(hadj->upper - hadj->page_size, 0)));
    if (vadj)
        gtk_adjustment_set_value(vadj, CLAMP(y * zoom + MARGIN - view->allocation.height / 2, 0,
                                             MAX(vadj->upper - vadj->page_size, 0)));
}

/* So many times life size, about what is in the middle of the window now. */
void
view_zoom(gint times)
{
    gint x, y;

    to_picture(view->allocation.width / 2, view->allocation.height / 2, &x, &y);
    zoom_about(times, MIN(x, image->w), MIN(y, image->h));
}

/*
 * Back to the picture as it was. What is half drawn is undone and
 * that is all; so is what was pasted or typed and not yet laid down.
 */
gboolean
view_undo(void)
{
    gboolean lifted = sel.on && sel.bits, pushed = sel.pushed;

    if (doing || stage) {
        cancel();
        return TRUE;
    }
    sel_free();
    if (lifted && !pushed)
        return TRUE;
    if (!undos)
        return FALSE;
    redos = g_list_prepend(redos, image);
    image = pop(&undos);
    image_clean(image);
    modified = TRUE;
    sized();
    return TRUE;
}

/* And forward again, to what was undone. */
gboolean
view_redo(void)
{
    view_finish();
    if (!redos)
        return FALSE;
    undos = g_list_prepend(undos, image);
    image = pop(&redos);
    image_clean(image);
    modified = TRUE;
    sized();
    return TRUE;
}

/* What is selected, as a picture of its own, with the background's colour where the mask keeps it out. */
Image *
view_selected(void)
{
    Image *all, *part;

    if (!sel.on || sel.text)
        return NULL;
    part = sel.bits ? image_copy(sel.bits) : image_crop(image, sel.x, sel.y, sel.w, sel.h, bg);
    if (!sel.mask)
        return part;
    all = image_new(sel.w, sel.h, bg);
    image_blend(all, part, sel.mask, 0, 0, NULL);
    image_free(part);
    return all;
}

/*
 * The clipboard is given what is selected, as a PNG file for whoever
 * asks; pasted here again, it is taken as it was, with its mask.
 */
void
view_copy(void)
{
    if (!sel.on || sel.text)
        return;
    image_free(clip_bits);
    image_free(clip_mask);
    clip_bits = sel.bits ? image_copy(sel.bits) : image_crop(image, sel.x, sel.y, sel.w, sel.h, bg);
    clip_mask = sel.mask ? image_copy(sel.mask) : NULL;
    gtk_selection_owner_set(view, clipboard, GDK_CURRENT_TIME);
}

/* What is selected is gone, and the background's colour is where it was. */
void
view_delete(void)
{
    if (!sel.on)
        return;
    if (!sel.bits)
        lift(FALSE);
    sel_free();
}

void
view_cut(void)
{
    if (sel.on && !sel.text) {
        view_copy();
        view_delete();
    }
}

/* A picture, which is taken, is pasted. */
void
view_paste_image(Image *img)
{
    afloat(img, NULL);
}

void
view_paste(void)
{
    if (clip_bits)
        afloat(image_copy(clip_bits), clip_mask ? image_copy(clip_mask) : NULL);
    else
        gtk_selection_convert(view, clipboard, png, GDK_CURRENT_TIME);
}

void
view_select_all(void)
{
    ui_tool(TOOL_SELECT);
    view_finish();
    sel.on = TRUE;
    sel.w = image->w;
    sel.h = image->h;
    sel_show();
}

/*
 * What Flip, Stretch and Invert work on: what is selected, which is
 * lifted for it, and otherwise the whole picture, which is TRUE.
 */
static gboolean
whole(void)
{
    if (sel.on && !sel.text && !doing) {
        lift(FALSE);
        return FALSE;
    }
    view_finish();
    undo_push(image_copy(image));
    return TRUE;
}

/* What is selected is another picture, perhaps of another size. */
static void
sel_set(Image *bits, Image *mask)
{
    sel_show();
    image_free(sel.bits);
    image_free(sel.mask);
    sel.bits = bits;
    sel.mask = mask;
    sel.w = bits->w;
    sel.h = bits->h;
    sel_show();
}

void
view_flip(gboolean vertical)
{
    if (whole())
        image_flip(image, vertical);
    else {
        image_flip(sel.bits, vertical);
        if (sel.mask)
            image_flip(sel.mask, vertical);
        sel_show();
    }
    flush();
}

void
view_rotate(gint quarters)
{
    if (whole())
        replace(image_rotate(image, quarters));
    else
        sel_set(image_rotate(sel.bits, quarters), sel.mask ? image_rotate(sel.mask, quarters) : NULL);
}

void
view_invert(void)
{
    if (whole())
        image_invert(image);
    else {
        image_invert(sel.bits);
        sel_show();
    }
    flush();
}

/*
 * Stretched to so many hundredths of its width and height, and then
 * slanted by so many degrees across and down. What is selected gets a
 * mask if it is slanted, so that its bare corners let the picture
 * through. FALSE, and nothing done, if it would be too large.
 */
gboolean
view_stretch(gdouble wide, gdouble high, gdouble across, gdouble downward)
{
    gboolean selected = sel.on && !sel.text && !doing, slant = across != 0 || downward != 0, all;
    gint w = CLAMP((selected ? sel.w : image->w) * wide / 100 + 0.5, 1, FAR);
    gint h = CLAMP((selected ? sel.h : image->h) * high / 100 + 0.5, 1, FAR), sw, sh;
    Image *bits, *mask, *next;

    if (w > IMAGE_MAX || h > IMAGE_MAX || !image_skew_size(w, h, across, downward, &sw, &sh))
        return FALSE;
    all = whole();
    bits = image_scale(all ? image : sel.bits, w, h);
    mask = all ? NULL : sel.mask ? image_scale(sel.mask, w, h) : slant ? image_new(w, h, 0xffffff) : NULL;
    if (slant) {
        next = image_skew(bits, across, downward, bg);
        image_free(bits);
        bits = next;
        if (mask) {
            next = image_skew(mask, across, downward, 0);
            image_free(mask);
            mask = next;
        }
    }
    if (all)
        replace(bits);
    else
        sel_set(bits, mask);
    return TRUE;
}

/* The picture is of another size: cut off, or with more of the background's colour. */
void
view_resize(gint w, gint h)
{
    view_finish();
    if (w == image->w && h == image->h)
        return;
    undo_push(image_copy(image));
    replace(image_crop(image, 0, 0, w, h, bg));
}

void
view_clear(void)
{
    view_finish();
    undo_push(image_copy(image));
    image_fill(image, 0, 0, image->w, image->h, bg);
    flush();
}

void
view_font(GdkFont *chosen)
{
    gdk_font_ref(chosen);
    if (font)
        gdk_font_unref(font);
    font = chosen;
    view_options();
}

/* One line of text as it is set: where it starts, and how many bytes of it. */
typedef struct {
    const gchar *s;
    gint len;
} Line;

/*
 * The text typed so far is set in its box: broken where it was
 * broken, and where a line would be wider than the box, at a space if
 * the line has one. The toolkit draws it in black on a white pixmap,
 * and how dark each point came out is how much of the text is there.
 * Text that is seen through is all of the foreground's colour behind
 * a mask of that; otherwise it is laid on the background's. The box
 * is as deep as its lines need, and never shallower than it was drawn.
 */
static void
text_render(void)
{
    GArray *lines = g_array_new(FALSE, FALSE, sizeof(Line));
    gint high = font->ascent + font->descent, w = sel.w, h, x, y, k;
    const gchar *s = typed->str, *p, *space, *next;
    GdkPixmap *pixmap;
    GdkImage *got;
    GdkGC *gc;
    Line line;
    guint i;

    for (p = s, space = NULL;; p = next) {
        next = *p ? g_utf8_next_char(p) : p;
        if (*p == '\n' || !*p) {
            line.s = s;
            line.len = p - s;
            g_array_append_val(lines, line);
            if (!*p)
                break;
            s = next;
            space = NULL;
        } else if (p > s && gdk_text_width(font, s, next - s) > w) {
            line.s = s;
            line.len = (space ? space : p) - s;
            g_array_append_val(lines, line);
            s = next = space ? space + 1 : p;
            space = NULL;
        } else if (*p == ' ')
            space = p;
    }
    line = g_array_index(lines, Line, lines->len - 1);
    caret_x = MIN(gdk_text_width(font, line.s, line.len), w - 1);
    caret_y = (lines->len - 1) * high;

    sel_show();
    h = sel.h = MAX(text_h, (gint) lines->len * high);
    pixmap = gdk_pixmap_new(view->window, w, h, -1);
    gc = gdk_gc_new(pixmap);
    gdk_rgb_gc_set_foreground(gc, 0xffffff);
    gdk_draw_rectangle(pixmap, gc, TRUE, 0, 0, w, h);
    gdk_rgb_gc_set_foreground(gc, 0x000000);
    for (i = 0; i < lines->len; i++) {
        line = g_array_index(lines, Line, i);
        gdk_draw_text(pixmap, font, gc, 0, i * high + font->ascent, line.s, line.len);
    }
    got = gdk_image_get(pixmap, 0, 0, w, h);

    image_free(sel.bits);
    image_free(sel.mask);
    sel.bits = image_new(w, h, see_through ? fg : bg);
    sel.mask = see_through ? image_new(w, h, 0) : NULL;
    for (y = 0; y < h && got; y++)
        for (x = 0; x < w; x++) {
            guint dark = 255 - (gdk_image_get_pixel(got, x, y) >> 8 & 255);
            guchar *q = (sel.mask ? sel.mask : sel.bits)->px + ((gsize) y * w + x) * 3;

            if (!dark)
                continue;
            for (k = 0; k < 3; k++)
                q[k] = sel.mask ? dark : ((fg >> (16 - 8 * k) & 255) * dark + q[k] * (255 - dark) + 127) / 255;
        }
    if (got)
        gdk_image_destroy(got);
    gdk_gc_unref(gc);
    gdk_pixmap_unref(pixmap);
    g_array_free(lines, TRUE);
    sel_show();
}

/* Which handle is at a point of the window: the right one, the bottom one, the corner's, or none. */
static gint
grip_at(gdouble wx, gdouble wy)
{
    gint x = (gint) wx + scroll_x() - MARGIN - image->w * zoom;
    gint y = (gint) wy + scroll_y() - MARGIN - image->h * zoom;
    gboolean right = x >= -NEAR && x < GRIP + NEAR, bottom = y >= -NEAR && y < GRIP + NEAR;

    if (right && bottom)
        return 3;
    if (right && abs(y + image->h * zoom / 2) <= GRIP + NEAR)
        return 1;
    if (bottom && abs(x + image->w * zoom / 2) <= GRIP + NEAR)
        return 2;
    return 0;
}

static gboolean
in_sel(gint x, gint y)
{
    return sel.on && x >= sel.x && y >= sel.y && x < sel.x + sel.w && y < sel.y + sel.h;
}

static void
pick(gint x, gint y)
{
    if (x < 0 || y < 0 || x >= image->w || y >= image->h)
        return;
    if (button == 1)
        fg = image_get(image, x, y);
    else
        bg = image_get(image, x, y);
    ui_chosen();
}

/* The pencil, a brush or the eraser goes on from where it was last to a point. */
static void
stroke_to(gint x, gint y)
{
    Pen pen = { ink, PEN_ROUND, 1, NULL };

    if (tool == TOOL_BRUSH) {
        pen.shape = brushes[brush].shape;
        pen.size = brushes[brush].size;
    } else if (tool == TOOL_ERASER) {
        /* The left button rubs out; the right one only what is of the foreground's colour. */
        pen.rgb = bg;
        pen.shape = PEN_SQUARE;
        pen.size = 4 + 2 * eraser;
        pen.only = button == 1 ? NULL : &fg;
    }
    image_line(image, last.x, last.y, x, y, &pen);
    last.x = x;
    last.y = y;
    flush();
}

/* The airbrush puffs for as long as the button is down, moved or not. */
static gint
puff(gpointer data G_GNUC_UNUSED)
{
    image_spray(image, last.x, last.y, 4 + 4 * spray, 6 + 6 * spray, ink);
    flush();
    return TRUE;
}

/* With Shift, a line keeps to eight directions and a shape is as wide as it is high. */
static void
straighten(const Pt *from, gint *x, gint *y)
{
    gint dx = *x - from->x, dy = *y - from->y, d = MAX(abs(dx), abs(dy));

    if (tool == TOOL_LINE || tool == TOOL_POLYGON || tool == TOOL_CURVE) {
        if (abs(dx) > 2 * abs(dy))
            dy = 0;
        else if (abs(dy) > 2 * abs(dx))
            dx = 0;
    }
    *x = from->x + (dx > 0 ? d : dx < 0 ? -d : 0);
    *y = from->y + (dy > 0 ? d : dy < 0 ? -d : 0);
}

/*
 * A line, a curve or a shape follows the pointer: what was drawn of
 * it is taken away and it is drawn again, to where the pointer is now.
 */
static void
band(gint x, gint y, guint state)
{
    Pen pen = { ink, PEN_ROUND, thick + 1, NULL };
    Pt *p = (Pt *) pts->data, *from = &down;
    guint i;

    if (tool == TOOL_POLYGON)
        from = &p[pts->len - 2];
    if ((state & GDK_SHIFT_MASK) && !(tool == TOOL_CURVE && stage))
        straighten(from, &x, &y);
    rubber();
    switch (tool) {
    case TOOL_LINE:
        image_line(image, down.x, down.y, x, y, &pen);
        break;
    case TOOL_CURVE:
        bend[stage == 0 ? 3 : stage].x = x;
        bend[stage == 0 ? 3 : stage].y = y;
        if (stage == 0)
            bend[1] = bend[0], bend[2] = bend[3];
        else if (stage == 1)
            bend[2] = bend[1];
        image_curve(image, bend, &pen);
        break;
    case TOOL_POLYGON:
        p[pts->len - 1].x = x;
        p[pts->len - 1].y = y;
        for (i = 0; i + 1 < pts->len; i++)
            image_line(image, p[i].x, p[i].y, p[i + 1].x, p[i + 1].y, &pen);
        break;
    default:
        image_shape(image, tool == TOOL_RECT ? SHAPE_RECT : tool == TOOL_ELLIPSE ? SHAPE_ELLIPSE : SHAPE_ROUND,
                    down.x, down.y, x, y, thick + 1, filling, ink, paper);
        break;
    }
    keep();
    ui_size(x - from->x, y - from->y);
}

/* A rectangle is drawn out, to select what is in it or to type in. */
static void
marquee(gint x, gint y)
{
    gint x0 = CLAMP(down.x, 0, image->w - 1), y0 = CLAMP(down.y, 0, image->h - 1);

    x = CLAMP(x, 0, image->w - 1);
    y = CLAMP(y, 0, image->h - 1);
    sel_show();
    sel.on = x != x0 || y != y0;
    sel.x = MIN(x, x0);
    sel.y = MIN(y, y0);
    sel.w = abs(x - x0) + 1;
    sel.h = abs(y - y0) + 1;
    sel_show();
    ui_size(sel.w, sel.h);
}

/* The magnifier: to four times the size, about the point, and back from any other size. */
static void
magnifier(gint x, gint y)
{
    zoom_about(button == 1 && zoom == 1 ? 4 : 1, x, y);
}

/* A button has gone down on a point of the picture, and the tool begins. */
static void
press(gint x, gint y, guint state)
{
    Pt at = { x, y };

    switch (tool) {
    case TOOL_AIRBRUSH:
        puffing = gtk_timeout_add(SPRAY, puff, NULL);
        /* fall through */
    case TOOL_PENCIL:
    case TOOL_BRUSH:
    case TOOL_ERASER:
        doing = DO_STROKE;
        stroke_begin();
        if (tool == TOOL_AIRBRUSH)
            puff(NULL);
        else
            stroke_to(x, y);
        break;
    case TOOL_FILL:
        if (x >= 0 && y >= 0 && x < image->w && y < image->h && image_get(image, x, y) != ink) {
            stroke_begin();
            image_flood(image, x, y, ink);
            stroke_end();
        }
        break;
    case TOOL_PICK:
        doing = DO_PICK;
        pick(x, y);
        break;
    case TOOL_ZOOM:
        magnifier(x, y);
        break;
    case TOOL_CURVE:
        doing = DO_BAND;
        if (!stage) {
            stroke_begin();
            bend[0] = at;
        }
        band(x, y, state);
        break;
    case TOOL_POLYGON:
        doing = DO_BAND;
        if (!stage) {
            stroke_begin();
            g_array_set_size(pts, 0);
            g_array_append_val(pts, at);
        }
        g_array_append_val(pts, at);
        band(x, y, state);
        break;
    case TOOL_LINE:
    case TOOL_RECT:
    case TOOL_ELLIPSE:
    case TOOL_ROUND:
        doing = DO_BAND;
        stroke_begin();
        band(x, y, state);
        break;
    default:  /* the two that select, and text */
        if (in_sel(x, y)) {
            doing = DO_MOVE;
            sel.hold.x = x - sel.x;
            sel.hold.y = y - sel.y;
            if (!sel.text)
                lift(state & GDK_CONTROL_MASK);
        } else {
            gint held = button;

            view_finish();
            button = held;
            if (tool == TOOL_FREE) {
                doing = DO_LASSO;
                at.x = CLAMP(x, 0, image->w - 1);
                at.y = CLAMP(y, 0, image->h - 1);
                g_array_set_size(pts, 0);
                g_array_append_val(pts, at);
            } else
                doing = DO_MARQUEE;
        }
        break;
    }
}

static gint
button_press(GtkWidget *widget, GdkEventButton *event)
{
    gint x, y;

    if (event->button > 3)  /* the wheel */
        return FALSE;
    gtk_widget_grab_focus(widget);
    if (event->type != GDK_BUTTON_PRESS) {
        if (event->type == GDK_2BUTTON_PRESS && tool == TOOL_POLYGON && stage)
            polygon_close();
        return TRUE;
    }
    if (event->button == 2)
        return TRUE;
    if (button) {  /* the other button, while one is down, undoes what that one was doing */
        cancel();
        return TRUE;
    }
    to_picture(event->x, event->y, &x, &y);
    button = event->button;
    down.x = last.x = x;
    down.y = last.y = y;
    if (!stage) {
        ink = button == 1 ? fg : bg;
        paper = button == 1 ? bg : fg;
    }
    if (button == 1 && !stage && (grip = grip_at(event->x, event->y))) {
        view_finish();
        button = 1;  /* which finishing let go of */
        doing = DO_RESIZE;
        new_w = image->w;
        new_h = image->h;
    } else
        press(x, y, event->state);
    return TRUE;
}

static gint
motion_notify(GtkWidget *widget G_GNUC_UNUSED, GdkEventMotion *event)
{
    Pt at;
    gint x, y;

    to_picture(event->x, event->y, &x, &y);
    ui_position(x, y);
    switch (doing) {
    case DO_STROKE:
        if (tool == TOOL_AIRBRUSH) {
            last.x = x;
            last.y = y;
        } else
            stroke_to(x, y);
        break;
    case DO_BAND:
        band(x, y, event->state);
        break;
    case DO_PICK:
        pick(x, y);
        break;
    case DO_MARQUEE:
        marquee(x, y);
        break;
    case DO_LASSO:
        at.x = CLAMP(x, 0, image->w - 1);
        at.y = CLAMP(y, 0, image->h - 1);
        show(MIN(at.x, last.x), MIN(at.y, last.y), abs(at.x - last.x) + 1, abs(at.y - last.y) + 1);
        g_array_append_val(pts, at);
        last = at;
        break;
    case DO_MOVE:
        sel_show();
        sel.x = x - sel.hold.x;
        sel.y = y - sel.hold.y;
        sel_show();
        break;
    case DO_RESIZE:
        show(0, 0, MAX(new_w, image->w), MAX(new_h, image->h));
        if (grip & 1)
            new_w = CLAMP(x, 1, IMAGE_MAX);
        if (grip & 2)
            new_h = CLAMP(y, 1, IMAGE_MAX);
        show(0, 0, new_w, new_h);
        ui_size(new_w, new_h);
        break;
    default:
        switch (grip_at(event->x, event->y)) {
        case 1: pointer(GDK_SB_H_DOUBLE_ARROW); break;
        case 2: pointer(GDK_SB_V_DOUBLE_ARROW); break;
        case 3: pointer(GDK_BOTTOM_RIGHT_CORNER); break;
        default:
            if (in_sel(x, y) && (tool == TOOL_SELECT || tool == TOOL_FREE))
                pointer(GDK_FLEUR);
            else
                tool_pointer();
            break;
        }
        break;
    }
    return TRUE;
}

static gint
button_release(GtkWidget *widget G_GNUC_UNUSED, GdkEventButton *event)
{
    Pt *p = (Pt *) pts->data;

    if ((gint) event->button != button)
        return TRUE;
    button = 0;
    if (puffing)
        gtk_timeout_remove(puffing);
    puffing = 0;
    switch (doing) {
    case DO_STROKE:
        stroke_end();
        break;
    case DO_BAND:
        if (tool == TOOL_CURVE) {
            if (stage == 0 && bend[0].x == bend[3].x && bend[0].y == bend[3].y)
                stroke_cancel();
            else if (++stage == 3) {
                stroke_end();
                stage = 0;
            }
        } else if (tool == TOOL_POLYGON) {
            if (stage && pts->len > 2 && abs(p[pts->len - 1].x - p[0].x) <= NEAR
                && abs(p[pts->len - 1].y - p[0].y) <= NEAR) {
                g_array_set_size(pts, pts->len - 1);
                polygon_close();
            } else
                stage = 1;
        } else
            stroke_end();
        break;
    case DO_PICK:
        ui_tool(back_to);
        break;
    case DO_MARQUEE:
        if (sel.on && tool == TOOL_TEXT) {
            sel.text = TRUE;
            text_h = sel.h;
            g_string_truncate(typed, 0);
            text_render();
        }
        break;
    case DO_LASSO:
        lasso_close();
        sel_show();
        break;
    case DO_RESIZE:
        show(0, 0, new_w, new_h);
        view_resize(new_w, new_h);
        break;
    }
    doing = DO_NOTHING;
    if (!stage)
        ui_size(0, 0);
    return TRUE;
}

static gint
leave_notify(GtkWidget *widget G_GNUC_UNUSED, GdkEventCrossing *event G_GNUC_UNUSED)
{
    ui_position(-1, -1);
    return FALSE;
}

/* Escape undoes what is half done, or ends the selection; Delete clears it; and text is typed. */
static gint
key_press(GtkWidget *widget G_GNUC_UNUSED, GdkEventKey *event)
{
    if (event->keyval == GDK_Escape) {
        if (doing || stage)
            cancel();
        else
            view_finish();
        return TRUE;
    }
    if ((event->keyval == GDK_Delete || event->keyval == GDK_KP_Delete) && sel.on) {
        view_delete();  /* which the toolkit will not have as a menu's key */
        return TRUE;
    }
    if (!sel.on || !sel.text)
        return FALSE;
    if (event->keyval == GDK_BackSpace) {
        if (typed->len)
            g_string_truncate(typed, g_utf8_validate(typed->str, -1, NULL)
                              ? (gsize) (g_utf8_prev_char(typed->str + typed->len) - typed->str)
                              : (gsize) typed->len - 1);
    } else if (event->keyval == GDK_Return || event->keyval == GDK_KP_Enter)
        g_string_append_c(typed, '\n');
    else if (event->length > 0 && !(event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK))
             && (guchar) event->string[0] >= ' ' && event->string[0] != 127)
        g_string_append(typed, event->string);
    else
        return FALSE;
    text_render();
    return TRUE;
}

/* What of the window is being drawn, as it is put together. */
typedef struct {
    guchar *px;
    gint x, y, w, h;
} Shot;

/* A point of the window is turned to its opposite, to show on anything. */
static void
turn(Shot *shot, gint x, gint y)
{
    guchar *p;

    if (x < shot->x || y < shot->y || x >= shot->x + shot->w || y >= shot->y + shot->h)
        return;
    p = shot->px + ((gsize) (y - shot->y) * shot->w + (x - shot->x)) * 3;
    p[0] = ~p[0];
    p[1] = ~p[1];
    p[2] = ~p[2];
}

/* A rectangle in dashes, which is how what is selected is marked. */
static void
dashes(Shot *shot, gint x0, gint y0, gint x1, gint y1)
{
    gint i;

    for (i = MAX(x0, shot->x); i <= x1 && i < shot->x + shot->w; i++)
        if ((i - x0) & 4) {
            turn(shot, i, y0);
            if (y1 != y0)
                turn(shot, i, y1);
        }
    for (i = MAX(y0 + 1, shot->y); i < y1 && i < shot->y + shot->h; i++)
        if ((i - y0) & 4) {
            turn(shot, x0, i);
            if (x1 != x0)
                turn(shot, x1, i);
        }
}

static void
block(Shot *shot, gint x, gint y, gint w, gint h, Rgb rgb)
{
    gint i, j;

    for (j = MAX(y, shot->y); j < y + h && j < shot->y + shot->h; j++)
        for (i = MAX(x, shot->x); i < x + w && i < shot->x + shot->w; i++) {
            guchar *p = shot->px + ((gsize) (j - shot->y) * shot->w + (i - shot->x)) * 3;

            p[0] = rgb >> 16;
            p[1] = rgb >> 8;
            p[2] = rgb;
        }
}

static Rgb
rgb_of(const GdkColor *colour)
{
    return (Rgb) (colour->red >> 8) << 16 | (colour->green >> 8) << 8 | colour->blue >> 8;
}

/*
 * The window is drawn: the picture at its magnification on the work
 * space, with what is lifted lying over it and, when it is large
 * enough, lines between its points; then what marks things, turned
 * over what is there: the selection's dashes, the way round a free
 * selection, the size being dragged out, and the caret. And the
 * handles, by which the picture is given another size.
 */
static gint
expose(GtkWidget *widget, GdkEventExpose *event)
{
    gint ox = MARGIN - scroll_x(), oy = MARGIN - scroll_y(), W = image->w * zoom, H = image->h * zoom;
    Rgb work = rgb_of(&widget->style->bg[GTK_STATE_ACTIVE]), mark = rgb_of(&widget->style->bg[GTK_STATE_SELECTED]);
    gboolean lifted = sel.on && sel.bits, lines = grid && zoom >= 4;
    const Rgb *see = clear();
    Shot shot;
    gint i, j, k;

    shot.x = MAX(event->area.x, 0);
    shot.y = MAX(event->area.y, 0);
    shot.w = MIN(event->area.x + event->area.width, widget->allocation.width) - shot.x;
    shot.h = MIN(event->area.y + event->area.height, widget->allocation.height) - shot.y;
    if (shot.w <= 0 || shot.h <= 0)
        return TRUE;
    shot.px = g_malloc((gsize) shot.w * shot.h * 3);

    for (j = 0; j < shot.h; j++) {
        gint vy = shot.y + j - oy, y = vy >= 0 && vy < H ? vy / zoom : -1;
        gboolean row = lifted && y >= sel.y && y < sel.y + sel.h;
        guchar *p = shot.px + (gsize) j * shot.w * 3;

        for (i = 0; i < shot.w; i++, p += 3) {
            gint vx = shot.x + i - ox, x = vx >= 0 && vx < W ? vx / zoom : -1, m = 0;
            const guchar *s;

            if (x < 0 || y < 0) {
                p[0] = work >> 16;
                p[1] = work >> 8;
                p[2] = work;
                continue;
            }
            if (lines && (vx % zoom == 0 || vy % zoom == 0)) {
                p[0] = p[1] = p[2] = 128;
                continue;
            }
            s = image->px + ((gsize) y * image->w + x) * 3;
            if (row && x >= sel.x && x < sel.x + sel.w) {
                gsize at = ((gsize) (y - sel.y) * sel.w + (x - sel.x)) * 3;
                const guchar *b = sel.bits->px + at;

                m = sel.mask ? sel.mask->px[at] : 255;
                if (see && ((Rgb) b[0] << 16 | b[1] << 8 | b[2]) == *see)
                    m = 0;
                if (m)
                    for (k = 0; k < 3; k++)
                        p[k] = (b[k] * m + s[k] * (255 - m) + 127) / 255;
            }
            if (!m) {
                p[0] = s[0];
                p[1] = s[1];
                p[2] = s[2];
            }
        }
    }

    if (sel.on && doing != DO_LASSO)
        dashes(&shot, sel.x * zoom + ox, sel.y * zoom + oy,
               (sel.x + sel.w) * zoom + ox - 1, (sel.y + sel.h) * zoom + oy - 1);
    if (sel.on && sel.text && doing != DO_MARQUEE)
        for (j = 0; j < (font->ascent + font->descent) * zoom; j++)
            turn(&shot, (sel.x + caret_x) * zoom + ox, (sel.y + caret_y) * zoom + oy + j);
    if (doing == DO_LASSO)
        for (k = 0; k + 1 < (gint) pts->len; k++) {
            const Pt *a = &g_array_index(pts, Pt, k), *b = &g_array_index(pts, Pt, k + 1);
            gint n = MAX(abs(b->x - a->x), abs(b->y - a->y)) * zoom, half = zoom / 2;

            for (i = 1; i <= n; i++)
                turn(&shot, (a->x * zoom + half) + (b->x - a->x) * zoom * i / n + ox,
                     (a->y * zoom + half) + (b->y - a->y) * zoom * i / n + oy);
        }
    if (doing == DO_RESIZE)
        dashes(&shot, ox, oy, new_w * zoom + ox - 1, new_h * zoom + oy - 1);
    block(&shot, ox + W, oy + H / 2 - GRIP / 2, GRIP, GRIP, mark);
    block(&shot, ox + W / 2 - GRIP / 2, oy + H, GRIP, GRIP, mark);
    block(&shot, ox + W, oy + H, GRIP, GRIP, mark);

    gdk_draw_rgb_image(widget->window, widget->style->black_gc, shot.x, shot.y, shot.w, shot.h,
                       GDK_RGB_DITHER_NONE, shot.px, shot.w * 3);
    g_free(shot.px);
    return TRUE;
}

/* Someone wants what was copied: as a PNG file. */
static void
selection_get(GtkWidget *widget G_GNUC_UNUSED, GtkSelectionData *data,
              guint info G_GNUC_UNUSED, guint time G_GNUC_UNUSED)
{
    Image *all;
    guchar *file;
    gsize len;

    if (!clip_bits)
        return;
    all = image_new(clip_bits->w, clip_bits->h, bg);
    image_blend(all, clip_bits, clip_mask, 0, 0, NULL);
    if ((file = file_png_data(all, &len)))
        gtk_selection_data_set(data, png, 8, file, len);
    g_free(file);
    image_free(all);
}

/* Something else was copied, somewhere else: ours is forgotten. */
static gint
selection_clear(GtkWidget *widget, GdkEventSelection *event)
{
    if (!gtk_selection_clear(widget, event))
        return FALSE;
    if (event->selection == clipboard) {
        image_free(clip_bits);
        image_free(clip_mask);
        clip_bits = clip_mask = NULL;
    }
    return TRUE;
}

/* What another program copied has come, if it had a picture to give. */
static void
selection_received(GtkWidget *widget G_GNUC_UNUSED, GtkSelectionData *data, guint time G_GNUC_UNUSED)
{
    Image *img;

    if (data->length > 0 && (img = file_load_png_data(data->data, data->length, NULL)))
        afloat(img, NULL);
}

static void
scrolled(GtkAdjustment *adj G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
    gtk_widget_queue_draw(view);
}

static void
allocated(GtkWidget *widget G_GNUC_UNUSED, GtkAllocation *allocation G_GNUC_UNUSED)
{
    sized();
}

static void
realized(GtkWidget *widget)
{
    if (!font)
        font = gdk_font_ref(widget->style->font);
    tool_pointer();
}

/*
 * The view is a drawing area that a scrolled window can scroll: it
 * takes the window's adjustments and draws what they say of the
 * picture, so no window is ever as large as a magnified picture.
 */
typedef struct {
    GtkDrawingAreaClass parent_class;
    void (*set_scroll_adjustments)(GtkWidget *widget, GtkAdjustment *h, GtkAdjustment *v);
} ViewClass;

static void
take(GtkAdjustment **mine, GtkAdjustment *adj)
{
    if (*mine == adj)
        return;
    if (*mine) {
        gtk_signal_disconnect_by_func(GTK_OBJECT(*mine), GTK_SIGNAL_FUNC(scrolled), NULL);
        gtk_object_unref(GTK_OBJECT(*mine));
    }
    if ((*mine = adj)) {
        gtk_object_ref(GTK_OBJECT(adj));
        gtk_object_sink(GTK_OBJECT(adj));
        gtk_signal_connect(GTK_OBJECT(adj), "value_changed", GTK_SIGNAL_FUNC(scrolled), NULL);
    }
}

static void
set_scroll_adjustments(GtkWidget *widget G_GNUC_UNUSED, GtkAdjustment *h, GtkAdjustment *v)
{
    take(&hadj, h);
    take(&vadj, v);
    if (image)
        sized();
}

static void
view_class_init(ViewClass *klass)
{
    GtkWidgetClass *widget_class = (GtkWidgetClass *) klass;

    widget_class->set_scroll_adjustments_signal =
        gtk_signal_new("set_scroll_adjustments", GTK_RUN_LAST, GTK_OBJECT_CLASS(klass)->type,
                       GTK_SIGNAL_OFFSET(ViewClass, set_scroll_adjustments),
                       gtk_marshal_NONE__POINTER_POINTER,
                       GTK_TYPE_NONE, 2, GTK_TYPE_ADJUSTMENT, GTK_TYPE_ADJUSTMENT);
    klass->set_scroll_adjustments = set_scroll_adjustments;
}

static GtkType
view_get_type(void)
{
    static GtkType type;

    if (!type) {
        static const GtkTypeInfo info = {
            "CgpaintView", sizeof(GtkDrawingArea), sizeof(ViewClass),
            (GtkClassInitFunc) view_class_init, NULL, NULL, NULL, NULL
        };

        type = gtk_type_unique(gtk_drawing_area_get_type(), &info);
    }
    return type;
}

/* The view, of which there is one, with a white picture in it to begin with. */
GtkWidget *
view_new(void)
{
    view = gtk_type_new(view_get_type());
    image = image_new(640, 480, bg);
    pts = g_array_new(FALSE, FALSE, sizeof(Pt));
    typed = g_string_new(NULL);
    font = gdk_font_load("Sans 12");
    clipboard = gdk_atom_intern("CLIPBOARD", FALSE);
    png = gdk_atom_intern("image/png", FALSE);

    GTK_WIDGET_SET_FLAGS(view, GTK_CAN_FOCUS);
    gtk_widget_set_events(view, GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
                          | GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK | GDK_KEY_PRESS_MASK);
    gtk_signal_connect(GTK_OBJECT(view), "expose_event", GTK_SIGNAL_FUNC(expose), NULL);
    gtk_signal_connect(GTK_OBJECT(view), "button_press_event", GTK_SIGNAL_FUNC(button_press), NULL);
    gtk_signal_connect(GTK_OBJECT(view), "button_release_event", GTK_SIGNAL_FUNC(button_release), NULL);
    gtk_signal_connect(GTK_OBJECT(view), "motion_notify_event", GTK_SIGNAL_FUNC(motion_notify), NULL);
    gtk_signal_connect(GTK_OBJECT(view), "leave_notify_event", GTK_SIGNAL_FUNC(leave_notify), NULL);
    gtk_signal_connect(GTK_OBJECT(view), "key_press_event", GTK_SIGNAL_FUNC(key_press), NULL);
    gtk_signal_connect_after(GTK_OBJECT(view), "size_allocate", GTK_SIGNAL_FUNC(allocated), NULL);
    gtk_signal_connect_after(GTK_OBJECT(view), "realize", GTK_SIGNAL_FUNC(realized), NULL);
    gtk_signal_connect(GTK_OBJECT(view), "selection_get", GTK_SIGNAL_FUNC(selection_get), NULL);
    gtk_signal_connect(GTK_OBJECT(view), "selection_clear_event", GTK_SIGNAL_FUNC(selection_clear), NULL);
    gtk_signal_connect(GTK_OBJECT(view), "selection_received", GTK_SIGNAL_FUNC(selection_received), NULL);
    gtk_selection_add_target(view, clipboard, png, 0);
    return view;
}
