/* cgpaint.c - a paint program: its window, and what is chosen there

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
#include <sys/stat.h>

#include "cgpaint.h"
#include "pictures.h"
#include "icons/cgpaint.xpm"

enum {
    SWATCH = 16,     /* a colour of the colour box, with its edge */
    PALETTE = 28,
    USED = 38,       /* the width of the two colours in use, before the others */
    BUTTON = 26,     /* a button of the tool box */
    OPTIONS_W = 50,  /* the box beneath the tools */
    OPTIONS_H = 70
};

static const struct {
    const gchar *name, *hint;
} tool_says[TOOLS] = {
    { N_("Free-Form Select"), N_("Selects a part of the picture of any shape, to move, copy or change.") },
    { N_("Select"), N_("Selects a rectangle of the picture, to move, copy or change.") },
    { N_("Eraser"), N_("Rubs out with the background color; with the right button, only the foreground color.") },
    { N_("Fill With Color"), N_("Fills an area of one color with another.") },
    { N_("Pick Color"), N_("Takes a color from the picture to draw with.") },
    { N_("Magnifier"), N_("Shows the picture larger, or at its own size again.") },
    { N_("Pencil"), N_("Draws a line one point wide where the pointer goes.") },
    { N_("Brush"), N_("Draws with a brush of the shape and size chosen.") },
    { N_("Airbrush"), N_("Sprays color, as widely as chosen.") },
    { N_("Text"), N_("Types text into the picture.") },
    { N_("Line"), N_("Draws a straight line of the width chosen.") },
    { N_("Curve"), N_("Draws a line of the width chosen, and then bends it, once or twice.") },
    { N_("Rectangle"), N_("Draws a rectangle, filled as chosen.") },
    { N_("Polygon"), N_("Draws a shape of straight sides, filled as chosen.") },
    { N_("Ellipse"), N_("Draws an ellipse, filled as chosen.") },
    { N_("Rounded Rectangle"), N_("Draws a rectangle with round corners, filled as chosen.") }
};

/* The colours Paint had in its colour box. */
static Rgb palette[PALETTE] = {
    0x000000, 0x808080, 0x800000, 0x808000, 0x008000, 0x008080, 0x000080,
    0x800080, 0x808040, 0x004040, 0x0080ff, 0x004080, 0x4000ff, 0x804000,
    0xffffff, 0xc0c0c0, 0xff0000, 0xffff00, 0x00ff00, 0x00ffff, 0x0000ff,
    0xff00ff, 0xffff80, 0x00ff80, 0x80ffff, 0x8080ff, 0xff0080, 0xff8040
};

static GtkWidget *win, *view, *side, *options, *colours, *status, *said, *where, *howbig, *opaque;
static GtkWidget *tools[TOOLS];
static gchar *file_name;  /* NULL until the picture has a file */
static gint swatch;       /* which colour of the box Edit Colors is to change */
static GdkPixmap *icon;
static GdkBitmap *icon_mask;

static Rgb
rgb_of(const GdkColor *colour)
{
    return (Rgb) (colour->red >> 8) << 16 | (colour->green >> 8) << 8 | colour->blue >> 8;
}

static void
set_title(void)
{
    gchar *base = file_name ? g_path_get_basename(file_name) : g_strdup(_("untitled"));
    gchar *title = g_strdup_printf(_("%s - Paint"), base);

    gtk_window_set_title(GTK_WINDOW(win), title);
    g_free(title);
    g_free(base);
}

/* Something is said, and OK is all there is to answer. */
static void
tell(const gchar *text)
{
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                                               GTK_BUTTONS_OK, "%s", text);

    gtk_window_set_title(GTK_WINDOW(dialog), _("Paint"));
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void
answered(GtkWidget *widget G_GNUC_UNUSED, gint *answer)
{
    *answer = TRUE;
    gtk_main_quit();
}

static void
refused(GtkWidget *widget G_GNUC_UNUSED, gint *answer)
{
    *answer = FALSE;
    gtk_main_quit();
}

static gint
closed(GtkWidget *widget G_GNUC_UNUSED, GdkEvent *event G_GNUC_UNUSED, gint *answer)
{
    *answer = FALSE;
    gtk_main_quit();
    return TRUE;
}

/*
 * One of the toolkit's windows that have an OK and a Cancel button of
 * their own, and are not dialogs, is made to wait for an answer: when
 * it is shown and gtk_main() runs, *answer is which button it was.
 */
static void
waits(GtkWidget *window, GtkWidget *ok, GtkWidget *cancel, gint *answer)
{
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(win));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_signal_connect(GTK_OBJECT(ok), "clicked", GTK_SIGNAL_FUNC(answered), answer);
    gtk_signal_connect(GTK_OBJECT(cancel), "clicked", GTK_SIGNAL_FUNC(refused), answer);
    gtk_signal_connect(GTK_OBJECT(window), "delete_event", GTK_SIGNAL_FUNC(closed), answer);
    gtk_widget_show(window);
}

/* A file is chosen, to open or to save as: its name, to be freed, or NULL. */
static gchar *
choose_file(const gchar *title, gboolean saving)
{
    GtkWidget *fs = gtk_file_selection_new(title);
    gchar *name = NULL, *dir;
    struct stat st;
    gint answer;

    gtk_file_selection_add_filter(GTK_FILE_SELECTION(fs), _("Pictures (*.bmp *.xpm *.png)"),
                                  "*.bmp *.dib *.xpm *.png *.BMP *.DIB *.XPM *.PNG");
    gtk_file_selection_add_filter(GTK_FILE_SELECTION(fs), _("All Files (*)"), "*");
    if (file_name)
        gtk_file_selection_set_filename(GTK_FILE_SELECTION(fs), file_name);
    else if (saving)
        gtk_file_selection_set_filename(GTK_FILE_SELECTION(fs), _("untitled.bmp"));
    waits(fs, GTK_FILE_SELECTION(fs)->ok_button, GTK_FILE_SELECTION(fs)->cancel_button, &answer);
    for (;;) {
        gtk_main();
        if (!answer)
            break;
        name = g_strdup(gtk_file_selection_get_filename(GTK_FILE_SELECTION(fs)));
        if (stat(name, &st) != 0 || !S_ISDIR(st.st_mode))
            break;
        /* A directory was named: that is where to look, not what to open. */
        dir = g_strconcat(name, name[strlen(name) - 1] == '/' ? "" : "/", NULL);
        gtk_file_selection_set_filename(GTK_FILE_SELECTION(fs), dir);
        g_free(dir);
        g_free(name);
        name = NULL;
    }
    gtk_widget_destroy(fs);
    if (name && !*name) {
        g_free(name);
        name = NULL;
    }
    return name;
}

static gboolean
load(const gchar *name)
{
    GError *error = NULL;
    Image *img = file_load(name, &error);

    if (!img) {
        gchar *text = g_strdup_printf(_("%s could not be opened.\n%s"), name, error->message);

        tell(text);
        g_free(text);
        g_error_free(error);
        return FALSE;
    }
    view_open(img);
    g_free(file_name);
    file_name = g_strdup(name);
    set_title();
    return TRUE;
}

/*
 * The picture is saved, under a new name if it is asked for or there
 * is none. How the name ends says what kind of file it is; a name
 * that does not say is given .bmp, as Paint gave it.
 */
static gboolean
save(gboolean as)
{
    GError *error = NULL;
    gchar *name, *text;
    struct stat st;
    gint answer;

    view_finish();
    if (!as && file_name)
        name = g_strdup(file_name);
    else {
        GtkWidget *dialog;

        if (!(name = choose_file(_("Save As"), TRUE)))
            return FALSE;
        if (file_format(name) == FORMAT_NONE) {
            text = g_strconcat(name, ".bmp", NULL);
            g_free(name);
            name = text;
        }
        if (stat(name, &st) == 0) {
            dialog = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                            GTK_BUTTONS_YES_NO, _("%s is there already.\nReplace it?"), name);
            gtk_window_set_title(GTK_WINDOW(dialog), _("Save As"));
            answer = gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            if (answer != GTK_RESPONSE_YES) {
                g_free(name);
                return FALSE;
            }
        }
    }
    if (!file_save(image, name, file_format(name), &error)) {
        text = g_strdup_printf(_("%s could not be saved.\n%s"), name, error->message);
        tell(text);
        g_free(text);
        g_error_free(error);
        g_free(name);
        return FALSE;
    }
    g_free(file_name);
    file_name = name;
    modified = FALSE;
    set_title();
    return TRUE;
}

/* Before the picture goes: is it to be saved? FALSE if it is to stay after all. */
static gboolean
may_go(void)
{
    GtkWidget *dialog;
    gchar *base;
    gint answer;

    view_finish();
    if (!modified)
        return TRUE;
    base = file_name ? g_path_get_basename(file_name) : g_strdup(_("untitled"));
    dialog = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                    GTK_BUTTONS_NONE, _("Save changes to %s?"), base);
    g_free(base);
    gtk_window_set_title(GTK_WINDOW(dialog), _("Paint"));
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("Yes"), GTK_RESPONSE_YES);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("No"), GTK_RESPONSE_NO);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_YES);
    answer = gtk_dialog_run(GTK_DIALOG(dialog));
    if (answer != GTK_RESPONSE_NONE)
        gtk_widget_destroy(dialog);
    return answer == GTK_RESPONSE_YES ? save(FALSE) : answer == GTK_RESPONSE_NO;
}

/* What the picture tells the window. */

void
ui_position(gint x, gint y)
{
    static gint was_x = -1, was_y = -1;
    gchar text[32] = "";

    if (x < 0 || y < 0 || x >= image->w || y >= image->h)
        x = y = -1;
    if (x == was_x && y == was_y)
        return;
    was_x = x;
    was_y = y;
    if (x >= 0)
        g_snprintf(text, sizeof text, "%d,%d", x, y);
    gtk_label_set_text(GTK_LABEL(where), text);
}

void
ui_size(gint w, gint h)
{
    static gint was_w, was_h;
    gchar text[32] = "";

    if (w == was_w && h == was_h)
        return;
    was_w = w;
    was_h = h;
    if (w || h)
        g_snprintf(text, sizeof text, "%dx%d", w, h);
    gtk_label_set_text(GTK_LABEL(howbig), text);
}

/* A colour or a choice of the tool's was made in the picture, or the Draw Opaque item is out of step. */
void
ui_chosen(void)
{
    gtk_widget_queue_draw(colours);
    gtk_widget_queue_draw(options);
    if (GTK_CHECK_MENU_ITEM(opaque)->active != !see_through)
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(opaque), !see_through);
}

void
ui_tool(gint which)
{
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tools[which]), TRUE);
}

/* The tool box. */

static void
tool_toggled(GtkWidget *b, gpointer which)
{
    if (!GTK_TOGGLE_BUTTON(b)->active)
        return;
    view_tool(GPOINTER_TO_INT(which));
    gtk_label_set_text(GTK_LABEL(said), _(tool_says[tool].hint));
    gtk_widget_queue_draw(options);
}

/*
 * The buttons' pictures are made, or made again for another theme:
 * each as an XPM in which # is the colour the theme writes in.
 */
static void
pictures_make(void)
{
    gchar *data[2 + G_N_ELEMENTS(picture_colours) + 16 + 1], ink[16];
    GdkPixmap *pixmap;
    GdkBitmap *mask;
    guint i, k, n;

    if (!win->window)
        return;
    g_snprintf(ink, sizeof ink, "# c #%06x", (guint) rgb_of(&win->style->fg[GTK_STATE_NORMAL]));
    for (i = 0; i < TOOLS; i++) {
        n = 0;
        data[n++] = "16 16 10 1";
        data[n++] = "  c None";
        data[n++] = ink;
        for (k = 0; k < G_N_ELEMENTS(picture_colours); k++)
            data[n++] = (gchar *) picture_colours[k];
        for (k = 0; k < 16; k++)
            data[n++] = (gchar *) pictures[i][k];
        pixmap = gdk_pixmap_create_from_xpm_d(win->window, &mask, NULL, data);
        if (GTK_BIN(tools[i])->child)
            gtk_pixmap_set(GTK_PIXMAP(GTK_BIN(tools[i])->child), pixmap, mask);
        else {
            GtkWidget *picture = gtk_pixmap_new(pixmap, mask);

            gtk_container_add(GTK_CONTAINER(tools[i]), picture);
            gtk_widget_show(picture);
        }
        gdk_pixmap_unref(pixmap);
        gdk_bitmap_unref(mask);
    }
}

static void
style_set(GtkWidget *widget G_GNUC_UNUSED, GtkStyle *was G_GNUC_UNUSED)
{
    pictures_make();
}

static GtkWidget *
tool_box(void)
{
    GtkWidget *table = gtk_table_new(TOOLS / 2, 2, TRUE);
    GtkTooltips *tips = gtk_tooltips_new();
    GSList *group = NULL;
    gint i;

    for (i = 0; i < TOOLS; i++) {
        tools[i] = gtk_radio_button_new(group);
        group = gtk_radio_button_group(GTK_RADIO_BUTTON(tools[i]));
        gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(tools[i]), FALSE);
        GTK_WIDGET_UNSET_FLAGS(tools[i], GTK_CAN_FOCUS);
        gtk_widget_set_usize(tools[i], BUTTON, BUTTON);
        gtk_tooltips_set_tip(tips, tools[i], _(tool_says[i].name), NULL);
        gtk_table_attach_defaults(GTK_TABLE(table), tools[i], i % 2, i % 2 + 1, i / 2, i / 2 + 1);
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tools[tool]), TRUE);
    for (i = 0; i < TOOLS; i++)
        gtk_signal_connect(GTK_OBJECT(tools[i]), "toggled", GTK_SIGNAL_FUNC(tool_toggled), GINT_TO_POINTER(i));
    return table;
}

/*
 * The box beneath the tools holds what the chosen tool can be told:
 * how many choices there are, in how many columns, and where the one
 * that is made is kept. NULL if the tool has nothing to be told.
 */
static gint *
choices(gint *n, gint *cols)
{
    *cols = 1;
    switch (tool) {
    case TOOL_FREE:
    case TOOL_SELECT:
    case TOOL_TEXT:
        *n = 2;
        return &see_through;
    case TOOL_ERASER:
        *n = 4;
        return &eraser;
    case TOOL_ZOOM:
        *n = 4;
        return &magnify;
    case TOOL_BRUSH:
        *n = 12;
        *cols = 3;
        return &brush;
    case TOOL_AIRBRUSH:
        *n = 3;
        return &spray;
    case TOOL_LINE:
    case TOOL_CURVE:
        *n = 5;
        return &thick;
    case TOOL_RECT:
    case TOOL_POLYGON:
    case TOOL_ELLIPSE:
    case TOOL_ROUND:
        *n = 3;
        return &filling;
    default:
        *n = 0;
        return NULL;
    }
}

/* Where the choices lie in the box: each is cw by ch, the first at 2, 2. */
static void
choice_cells(gint n, gint cols, gint *cw, gint *ch)
{
    gint rows = (n + cols - 1) / cols;

    *cw = (OPTIONS_W - 4) / cols;
    *ch = MIN((OPTIONS_H - 4) / rows, 22);
}

/*
 * The box is drawn as a picture, by what draws the picture, so that a
 * brush or a line is shown as it will come out: each choice in what
 * the theme writes with, and the one that is made in its colours for
 * what is selected.
 */
static gint
options_expose(GtkWidget *widget, GdkEventExpose *event G_GNUC_UNUSED)
{
    GtkStyle *style = widget->style;
    Rgb face = rgb_of(&style->bg[GTK_STATE_NORMAL]);
    Image *box = image_new(OPTIONS_W, OPTIONS_H, face);
    gint n, cols, cw, ch, i, k, *made = choices(&n, &cols);

    if (made)
        choice_cells(n, cols, &cw, &ch);
    for (i = 0; made && i < n; i++) {
        gint x = 2 + i % cols * cw, y = 2 + i / cols * ch, mx = x + cw / 2, my = y + ch / 2;
        Rgb back = i == *made ? rgb_of(&style->bg[GTK_STATE_SELECTED]) : face;
        Rgb ink = rgb_of(&style->fg[i == *made ? GTK_STATE_SELECTED : GTK_STATE_NORMAL]);
        Rgb grey = (ink >> 1 & 0x7f7f7f) + (back >> 1 & 0x7f7f7f);
        Pen pen = { ink, PEN_ROUND, 1, NULL };
        guint seed = 1;

        image_fill(box, x, y, cw, ch, back);
        if (made == &see_through) {
            /* One thing over another: it hides it, or lets it through. */
            image_fill(box, mx - 9, my - 7, 10, 10, grey);
            if (i == 0)
                image_fill(box, mx - 3, my - 3, 12, 10, back);
            image_shape(box, SHAPE_RECT, mx - 3, my - 3, mx + 8, my + 6, 1, FILL_NONE, ink, 0);
        } else if (made == &eraser)
            image_fill(box, mx - 2 - i, my - 2 - i, 4 + 2 * i, 4 + 2 * i, ink);
        else if (made == &brush) {
            pen.shape = brushes[i].shape;
            pen.size = brushes[i].size;
            image_dab(box, mx, my, &pen);
        } else if (made == &spray)
            for (k = 0; k < 14 + 12 * i; k++) {
                gint r = 3 + 2 * i, dx, dy;

                do {
                    seed = seed * 1103515245 + 12345;
                    dx = (gint) (seed >> 16) % (2 * r + 1) - r;
                    seed = seed * 1103515245 + 12345;
                    dy = (gint) (seed >> 16) % (2 * r + 1) - r;
                } while (dx * dx + dy * dy > r * r);
                image_span(box, mx + dx, mx + dx, my + dy, ink);
            }
        else if (made == &thick)
            image_fill(box, x + 4, my - i / 2, cw - 8, i + 1, ink);
        else if (made == &filling)
            image_shape(box, SHAPE_RECT, x + 6, y + 4, x + cw - 7, y + ch - 5, 1, i,
                        i == FILL_ONLY ? grey : ink, grey);
    }
    gdk_draw_rgb_image(widget->window, style->black_gc, 0, 0, OPTIONS_W, OPTIONS_H,
                       GDK_RGB_DITHER_NONE, box->px, OPTIONS_W * 3);
    image_free(box);
    for (i = 0; made == &magnify && i < n; i++) {
        gchar text[8];

        g_snprintf(text, sizeof text, "%dx", magnified[i]);
        gdk_draw_string(widget->window, style->font, style->fg_gc[i == *made ? GTK_STATE_SELECTED : GTK_STATE_NORMAL],
                        (OPTIONS_W - gdk_string_width(style->font, text)) / 2,
                        2 + i * ch + (ch + style->font->ascent - style->font->descent) / 2, text);
    }
    gtk_paint_shadow(style, widget->window, GTK_STATE_NORMAL, GTK_SHADOW_IN, NULL, widget, NULL,
                     0, 0, OPTIONS_W, OPTIONS_H);
    return TRUE;
}

static gint
options_press(GtkWidget *widget G_GNUC_UNUSED, GdkEventButton *event)
{
    gint n, cols, cw, ch, i, *made = choices(&n, &cols);

    if (!made || event->type != GDK_BUTTON_PRESS || event->x < 2 || event->y < 2)
        return TRUE;
    choice_cells(n, cols, &cw, &ch);
    i = ((gint) event->y - 2) / ch * cols + MIN(((gint) event->x - 2) / cw, cols - 1);
    if (i >= n)
        return TRUE;
    *made = i;
    if (made == &magnify)  /* the magnifier's choice is made at once, as Paint made it */
        view_zoom(magnified[i]);
    view_options();
    ui_chosen();
    return TRUE;
}

/* The colour box. */

static void
swatch_draw(GtkWidget *widget, GdkGC *gc, gint x, gint y, gint size, Rgb rgb)
{
    gdk_rgb_gc_set_foreground(gc, rgb);
    gdk_draw_rectangle(widget->window, gc, TRUE, x, y, size, size);
    gdk_draw_rectangle(widget->window, widget->style->fg_gc[GTK_STATE_NORMAL], FALSE, x, y, size - 1, size - 1);
}

/* The two colours in use, the foreground's over the background's, and the rest in two rows. */
static gint
colours_expose(GtkWidget *widget, GdkEventExpose *event G_GNUC_UNUSED)
{
    static GdkGC *gc;
    gint i;

    if (!gc)
        gc = gdk_gc_new(widget->window);
    gtk_paint_shadow(widget->style, widget->window, GTK_STATE_NORMAL, GTK_SHADOW_IN, NULL, widget, NULL,
                     0, 0, USED - 4, 2 * SWATCH + 2);
    swatch_draw(widget, gc, 14, 14, 15, bg);
    swatch_draw(widget, gc, 5, 5, 15, fg);
    for (i = 0; i < PALETTE; i++)
        swatch_draw(widget, gc, USED + i % (PALETTE / 2) * SWATCH, 1 + i / (PALETTE / 2) * SWATCH,
                    SWATCH - 1, palette[i]);
    return TRUE;
}

/* The toolkit's colour chooser is asked for a colour, starting from one. */
static gboolean
choose_colour(Rgb *rgb)
{
    GtkWidget *dialog = gtk_color_selection_dialog_new(_("Edit Colors"));
    GtkColorSelection *sel = GTK_COLOR_SELECTION(GTK_COLOR_SELECTION_DIALOG(dialog)->colorsel);
    gdouble c[4] = { (*rgb >> 16) / 255.0, (*rgb >> 8 & 255) / 255.0, (*rgb & 255) / 255.0, 1 };
    gint answer;

    gtk_color_selection_set_color(sel, c);
    gtk_widget_hide(GTK_COLOR_SELECTION_DIALOG(dialog)->help_button);
    waits(dialog, GTK_COLOR_SELECTION_DIALOG(dialog)->ok_button,
          GTK_COLOR_SELECTION_DIALOG(dialog)->cancel_button, &answer);
    gtk_main();
    if (answer) {
        gtk_color_selection_get_color(sel, c);
        *rgb = (Rgb) (c[0] * 255 + 0.5) << 16 | (Rgb) (c[1] * 255 + 0.5) << 8 | (Rgb) (c[2] * 255 + 0.5);
    }
    gtk_widget_destroy(dialog);
    return answer;
}

/* One of the box's colours is made another, and is the foreground's. */
static void
edit_colour(void)
{
    if (!choose_colour(&palette[swatch]))
        return;
    fg = palette[swatch];
    ui_chosen();
    view_options();
}

/* The left button takes a colour for the foreground, the right one for the background; twice changes it. */
static gint
colours_press(GtkWidget *widget G_GNUC_UNUSED, GdkEventButton *event)
{
    gint col = ((gint) event->x - USED) / SWATCH, row = ((gint) event->y - 1) / SWATCH;

    if (event->x < USED || col >= PALETTE / 2 || row < 0 || row > 1 || event->button == 2 || event->button > 3)
        return TRUE;
    if (event->type == GDK_2BUTTON_PRESS && event->button == 1) {
        edit_colour();
        return TRUE;
    }
    if (event->type != GDK_BUTTON_PRESS)
        return TRUE;
    if (event->button == 1) {
        swatch = row * (PALETTE / 2) + col;
        fg = palette[swatch];
    } else
        bg = palette[row * (PALETTE / 2) + col];
    ui_chosen();
    view_options();
    return TRUE;
}

/* A dialog of this program's own: OK and Cancel, and room above them. */
static GtkWidget *
dialog_new(const gchar *title, GtkWidget **box)
{
    GtkWidget *dialog = gtk_dialog_new();

    gtk_window_set_title(GTK_WINDOW(dialog), title);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(win));
    gtk_window_set_policy(GTK_WINDOW(dialog), FALSE, FALSE, TRUE);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("OK"), GTK_RESPONSE_OK);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    *box = gtk_vbox_new(FALSE, 4);
    gtk_container_set_border_width(GTK_CONTAINER(*box), 10);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), *box, TRUE, TRUE, 0);
    return dialog;
}

/* It is shown, and answered: TRUE for OK, and it is still there to be asked things. */
static gboolean
dialog_ok(GtkWidget *dialog)
{
    gtk_widget_show_all(GTK_DIALOG(dialog)->vbox);
    return gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK;
}

/* Some choices of which one is made, one beneath another: which is made, on OK. */
static GSList *
radios(GtkWidget *box, const gchar *const *labels, gint n, gint made)
{
    GSList *group = NULL;
    GtkWidget *b;
    gint i;

    for (i = 0; i < n; i++) {
        b = gtk_radio_button_new_with_label(group, _(labels[i]));
        group = gtk_radio_button_group(GTK_RADIO_BUTTON(b));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b), i == made);
        gtk_box_pack_start(GTK_BOX(box), b, FALSE, FALSE, 0);
    }
    return group;
}

/* The group lists the last made first. */
static gint
radio_made(GSList *group)
{
    gint i = g_slist_length(group);

    for (; group; group = group->next)
        if (i--, GTK_TOGGLE_BUTTON(group->data)->active)
            return i;
    return 0;
}

/* A number to be set, with what it is before it and what it is in after. */
static GtkWidget *
number(GtkWidget *table, gint row, const gchar *label, const gchar *unit, gfloat value, gfloat low, gfloat high)
{
    GtkObject *adj = gtk_adjustment_new(value, low, high, 1, 10, 0);
    GtkWidget *spin = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 1, 0), *l = gtk_label_new(label);

    gtk_misc_set_alignment(GTK_MISC(l), 0.0, 0.5);
    gtk_table_attach_defaults(GTK_TABLE(table), l, 0, 1, row, row + 1);
    gtk_widget_set_usize(spin, 70, -1);
    gtk_table_attach_defaults(GTK_TABLE(table), spin, 1, 2, row, row + 1);
    l = gtk_label_new(unit);
    gtk_misc_set_alignment(GTK_MISC(l), 0.0, 0.5);
    gtk_table_attach_defaults(GTK_TABLE(table), l, 2, 3, row, row + 1);
    return spin;
}

static GtkWidget *
numbers(GtkWidget *box, const gchar *title)
{
    GtkWidget *frame = gtk_frame_new(title), *table = gtk_table_new(2, 3, FALSE);

    gtk_table_set_row_spacings(GTK_TABLE(table), 4);
    gtk_table_set_col_spacings(GTK_TABLE(table), 8);
    gtk_container_set_border_width(GTK_CONTAINER(table), 8);
    gtk_container_add(GTK_CONTAINER(frame), table);
    gtk_box_pack_start(GTK_BOX(box), frame, FALSE, FALSE, 0);
    return table;
}

/* The menus. */

static void
file_new(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *item G_GNUC_UNUSED)
{
    if (!may_go())
        return;
    view_open(image_new(image->w, image->h, bg));
    g_free(file_name);
    file_name = NULL;
    set_title();
}

static void
file_open(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *item G_GNUC_UNUSED)
{
    gchar *name;

    if (may_go() && (name = choose_file(_("Open"), FALSE))) {
        load(name);
        g_free(name);
    }
}

static void
file_save_as(gpointer data G_GNUC_UNUSED, guint as, GtkWidget *item G_GNUC_UNUSED)
{
    save(as);
}

static void
file_exit(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *item G_GNUC_UNUSED)
{
    if (may_go())
        gtk_main_quit();
}

static gint
delete_event(GtkWidget *widget G_GNUC_UNUSED, GdkEvent *event G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
    if (may_go())
        gtk_main_quit();
    return TRUE;
}

enum { EDIT_UNDO, EDIT_REPEAT, EDIT_CUT, EDIT_COPY, EDIT_PASTE, EDIT_CLEAR, EDIT_ALL, EDIT_TO, EDIT_FROM };

static void
edit(gpointer data G_GNUC_UNUSED, guint action, GtkWidget *item G_GNUC_UNUSED)
{
    GError *error = NULL;
    Image *img;
    gchar *name, *text, *kept;

    switch (action) {
    case EDIT_UNDO: view_undo(); break;
    case EDIT_REPEAT: view_redo(); break;
    case EDIT_CUT: view_cut(); break;
    case EDIT_COPY: view_copy(); break;
    case EDIT_PASTE: view_paste(); break;
    case EDIT_CLEAR: view_delete(); break;
    case EDIT_ALL: view_select_all(); break;
    case EDIT_TO:  /* what is selected, to a file of its own */
        if (!(img = view_selected()))
            break;
        kept = file_name;
        file_name = NULL;
        name = choose_file(_("Copy To"), TRUE);
        file_name = kept;
        if (name && file_format(name) == FORMAT_NONE) {
            text = g_strconcat(name, ".bmp", NULL);
            g_free(name);
            name = text;
        }
        if (name && !file_save(img, name, file_format(name), &error)) {
            text = g_strdup_printf(_("%s could not be saved.\n%s"), name, error->message);
            tell(text);
            g_free(text);
            g_error_free(error);
        }
        g_free(name);
        image_free(img);
        break;
    case EDIT_FROM:  /* a file, as if it had been copied */
        if (!(name = choose_file(_("Paste From"), FALSE)))
            break;
        if ((img = file_load(name, &error)))
            view_paste_image(img);
        else {
            text = g_strdup_printf(_("%s could not be opened.\n%s"), name, error->message);
            tell(text);
            g_free(text);
            g_error_free(error);
        }
        g_free(name);
        break;
    }
}

enum { SHOW_TOOLS, SHOW_COLOURS, SHOW_STATUS, SHOW_GRID, SHOW_OPAQUE };

/* The items with a tick: what they show is shown as they are ticked. */
static void
shown(gpointer data G_GNUC_UNUSED, guint what, GtkWidget *item)
{
    gboolean on = GTK_CHECK_MENU_ITEM(item)->active;
    GtkWidget *part = what == SHOW_TOOLS ? side : what == SHOW_COLOURS ? colours->parent : status;

    if (what == SHOW_GRID) {
        grid = on;
        view_options();
    } else if (what == SHOW_OPAQUE) {
        see_through = !on;
        view_options();
        gtk_widget_queue_draw(options);
    } else if (on)
        gtk_widget_show(part);
    else
        gtk_widget_hide(part);
}

static void
zoom_to(gpointer data G_GNUC_UNUSED, guint times, GtkWidget *item G_GNUC_UNUSED)
{
    static const gchar *const labels[] = { "100%", "200%", "400%", "600%", "800%" };
    static const gint sizes[] = { 1, 2, 4, 6, 8 };
    GtkWidget *dialog, *box;
    GSList *group;
    gint i;

    if (!times) {
        dialog = dialog_new(_("Custom Zoom"), &box);
        for (i = 4; i > 0 && sizes[i] > zoom; i--)
            ;
        group = radios(box, labels, 5, i);
        if (dialog_ok(dialog))
            times = sizes[radio_made(group)];
        gtk_widget_destroy(dialog);
    }
    if (times)
        view_zoom(times);
}

static void
text_font(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *item G_GNUC_UNUSED)
{
    static gchar *name;
    GtkWidget *dialog = gtk_font_selection_dialog_new(_("Text Font"));
    GtkFontSelectionDialog *fsd = GTK_FONT_SELECTION_DIALOG(dialog);
    GdkFont *font;
    gint answer;

    gtk_font_selection_dialog_set_font_name(fsd, name ? name : "Sans 12");
    gtk_widget_hide(fsd->apply_button);
    waits(dialog, fsd->ok_button, fsd->cancel_button, &answer);
    gtk_main();
    if (answer && (font = gtk_font_selection_dialog_get_font(fsd))) {
        g_free(name);
        name = gtk_font_selection_dialog_get_font_name(fsd);
        view_font(font);
    }
    gtk_widget_destroy(dialog);
}

enum { IMAGE_FLIP, IMAGE_STRETCH, IMAGE_INVERT, IMAGE_ATTRIBUTES, IMAGE_CLEAR };

static void
image_menu(gpointer data G_GNUC_UNUSED, guint action, GtkWidget *item G_GNUC_UNUSED)
{
    static const gchar *const turns[] = {
        N_("Flip horizontal"), N_("Flip vertical"), N_("Rotate by 90 degrees"),
        N_("Rotate by 180 degrees"), N_("Rotate by 270 degrees")
    };
    GtkWidget *dialog, *box, *table, *n[4];
    GSList *group;
    gint i;

    switch (action) {
    case IMAGE_FLIP:
        dialog = dialog_new(_("Flip and Rotate"), &box);
        group = radios(box, turns, 5, 0);
        if (dialog_ok(dialog)) {
            if ((i = radio_made(group)) < 2)
                view_flip(i);
            else
                view_rotate(i - 1);
        }
        gtk_widget_destroy(dialog);
        break;
    case IMAGE_STRETCH:
        dialog = dialog_new(_("Stretch and Skew"), &box);
        table = numbers(box, _("Stretch"));
        n[0] = number(table, 0, _("Horizontal:"), "%", 100, 1, 500);
        n[1] = number(table, 1, _("Vertical:"), "%", 100, 1, 500);
        table = numbers(box, _("Skew"));
        n[2] = number(table, 0, _("Horizontal:"), _("degrees"), 0, -89, 89);
        n[3] = number(table, 1, _("Vertical:"), _("degrees"), 0, -89, 89);
        if (dialog_ok(dialog)
            && !view_stretch(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(n[0])),
                             gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(n[1])),
                             gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(n[2])),
                             gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(n[3]))))
            tell(_("The picture would be too large."));
        gtk_widget_destroy(dialog);
        break;
    case IMAGE_INVERT:
        view_invert();
        break;
    case IMAGE_ATTRIBUTES:
        dialog = dialog_new(_("Attributes"), &box);
        table = numbers(box, _("Size"));
        n[0] = number(table, 0, _("Width:"), _("pixels"), image->w, 1, IMAGE_MAX);
        n[1] = number(table, 1, _("Height:"), _("pixels"), image->h, 1, IMAGE_MAX);
        if (dialog_ok(dialog))
            view_resize(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(n[0])),
                        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(n[1])));
        gtk_widget_destroy(dialog);
        break;
    case IMAGE_CLEAR:
        view_clear();
        break;
    }
}

static void
colours_edit(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *item G_GNUC_UNUSED)
{
    edit_colour();
}

static void
about(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *item G_GNUC_UNUSED)
{
    GtkWidget *dialog = gtk_dialog_new(), *box = gtk_vbox_new(FALSE, 4);
    gchar *text = g_strdup_printf("CGPaint %s", PACKAGE_VERSION);

    gtk_window_set_title(GTK_WINDOW(dialog), _("About CGPaint"));
    gtk_window_set_policy(GTK_WINDOW(dialog), FALSE, FALSE, TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(win));
    gtk_container_set_border_width(GTK_CONTAINER(box), 16);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), box, TRUE, TRUE, 0);
    if (icon)
        gtk_box_pack_start(GTK_BOX(box), gtk_pixmap_new(icon, icon_mask), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new(text), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new(_("A paint program, as Paint was")), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new("(C) 2026 The CGTK authors"), FALSE, FALSE, 4);
    g_free(text);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("OK"), GTK_RESPONSE_OK);
    gtk_widget_show_all(box);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_NONE)
        gtk_widget_destroy(dialog);
}

typedef struct {
    const gchar *parent, *label, *accelerator;
    void (*callback)(gpointer, guint, GtkWidget *);
    guint action;
    const gchar *type;
} MenuEntry;

static const MenuEntry entries[] = {
    { "", N_("_File"), NULL, NULL, 0, "<Branch>" },
    { "/File", N_("_New"), "<control>N", file_new, 0, NULL },
    { "/File", N_("_Open..."), "<control>O", file_open, 0, NULL },
    { "/File", N_("_Save"), "<control>S", file_save_as, FALSE, NULL },
    { "/File", N_("Save _As..."), NULL, file_save_as, TRUE, NULL },
    { "/File", "-", NULL, NULL, 0, "<Separator>" },
    { "/File", N_("E_xit"), "<control>Q", file_exit, 0, NULL },
    { "", N_("_Edit"), NULL, NULL, 0, "<Branch>" },
    { "/Edit", N_("_Undo"), "<control>Z", edit, EDIT_UNDO, NULL },
    { "/Edit", N_("_Repeat"), "<control>Y", edit, EDIT_REPEAT, NULL },
    { "/Edit", "-", NULL, NULL, 0, "<Separator>" },
    { "/Edit", N_("Cu_t"), "<control>X", edit, EDIT_CUT, NULL },
    { "/Edit", N_("_Copy"), "<control>C", edit, EDIT_COPY, NULL },
    { "/Edit", N_("_Paste"), "<control>V", edit, EDIT_PASTE, NULL },
    { "/Edit", N_("C_lear Selection"), NULL, edit, EDIT_CLEAR, NULL },
    { "/Edit", N_("Select _All"), "<control>A", edit, EDIT_ALL, NULL },
    { "/Edit", "--", NULL, NULL, 0, "<Separator>" },
    { "/Edit", N_("C_opy To..."), NULL, edit, EDIT_TO, NULL },
    { "/Edit", N_("Paste _From..."), NULL, edit, EDIT_FROM, NULL },
    { "", N_("_View"), NULL, NULL, 0, "<Branch>" },
    { "/View", N_("_Tool Box"), "<control>T", shown, SHOW_TOOLS, "<CheckItem>" },
    { "/View", N_("_Color Box"), "<control>L", shown, SHOW_COLOURS, "<CheckItem>" },
    { "/View", N_("_Status Bar"), NULL, shown, SHOW_STATUS, "<CheckItem>" },
    { "/View", "-", NULL, NULL, 0, "<Separator>" },
    { "/View", N_("_Zoom"), NULL, NULL, 0, "<Branch>" },
    { "/View/Zoom", N_("_Normal Size"), "<control>Page_Up", zoom_to, 1, NULL },
    { "/View/Zoom", N_("_Large Size"), "<control>Page_Down", zoom_to, 4, NULL },
    { "/View/Zoom", N_("C_ustom..."), NULL, zoom_to, 0, NULL },
    { "/View/Zoom", "-", NULL, NULL, 0, "<Separator>" },
    { "/View/Zoom", N_("Show _Grid"), "<control>G", shown, SHOW_GRID, "<CheckItem>" },
    { "/View", N_("Text _Font..."), NULL, text_font, 0, NULL },
    { "", N_("_Image"), NULL, NULL, 0, "<Branch>" },
    { "/Image", N_("_Flip and Rotate..."), "<control>R", image_menu, IMAGE_FLIP, NULL },
    { "/Image", N_("_Stretch and Skew..."), "<control>W", image_menu, IMAGE_STRETCH, NULL },
    { "/Image", N_("_Invert Colors"), "<control>I", image_menu, IMAGE_INVERT, NULL },
    { "/Image", N_("_Attributes..."), "<control>E", image_menu, IMAGE_ATTRIBUTES, NULL },
    { "/Image", N_("_Clear Image"), "<control><shift>N", image_menu, IMAGE_CLEAR, NULL },
    { "/Image", N_("_Draw Opaque"), NULL, shown, SHOW_OPAQUE, "<CheckItem>" },
    { "", N_("_Colors"), NULL, NULL, 0, "<Branch>" },
    { "/Colors", N_("_Edit Colors..."), NULL, colours_edit, 0, NULL },
    { "", N_("_Help"), NULL, NULL, 0, "<Branch>" },
    { "/Help", N_("_About..."), NULL, about, 0, NULL }
};

/* Only the last part of a path is a label. */
static gchar *
menu_translate(const gchar *path, gpointer data G_GNUC_UNUSED)
{
    const gchar *label = strrchr(path, '/');

    return _(label ? label + 1 : path);
}

static GtkWidget *
menus(GtkAccelGroup *accel)
{
    static const gchar *const ticked[] = { "/View/Tool Box", "/View/Color Box", "/View/Status Bar", "/Image/Draw Opaque" };
    GtkItemFactory *factory = gtk_item_factory_new(GTK_TYPE_MENU_BAR, "<main>", accel);
    GtkItemFactoryEntry entry;
    guint i;

    gtk_item_factory_set_translate_func(factory, menu_translate, NULL, NULL);
    for (i = 0; i < G_N_ELEMENTS(entries); i++) {
        gchar *path = g_strconcat(entries[i].parent, "/", entries[i].label, NULL);

        entry.path = path;
        entry.accelerator = (gchar *) entries[i].accelerator;
        entry.callback = (GtkItemFactoryCallback) entries[i].callback;
        entry.callback_action = entries[i].action;
        entry.item_type = (gchar *) entries[i].type;
        gtk_item_factory_create_item(factory, &entry, NULL, 1);
        g_free(path);
    }
    opaque = gtk_item_factory_get_widget(factory, "/Image/Draw Opaque");
    for (i = 0; i < G_N_ELEMENTS(ticked); i++)
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(gtk_item_factory_get_widget(factory, ticked[i])), TRUE);
    return gtk_item_factory_get_widget(factory, "<main>");
}

/* A sunken place in the status bar, with something written in it. */
static GtkWidget *
status_part(GtkWidget *box, gint wide)
{
    GtkWidget *frame = gtk_frame_new(NULL), *label = gtk_label_new("");

    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_misc_set_padding(GTK_MISC(label), 4, 1);
    gtk_container_add(GTK_CONTAINER(frame), label);
    if (wide)
        gtk_widget_set_usize(frame, wide, -1);
    gtk_box_pack_start(GTK_BOX(box), frame, !wide, !wide, 0);
    return label;
}

/*
 * The window, as Paint's was: the menus; the tools at the left, with
 * what the chosen one can be told beneath them, and the picture beside
 * them; the colours; and a line that says what the tool is for, where
 * the pointer is and how large the thing being drawn.
 */
static void
build(void)
{
    GtkWidget *box, *row, *scrolled, *align;
    GtkAccelGroup *accel = gtk_accel_group_new();

    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_wmclass(GTK_WINDOW(win), "cgpaint", "Cgpaint");
    gtk_window_set_default_size(GTK_WINDOW(win), 780, 620);
    gtk_window_add_accel_group(GTK_WINDOW(win), accel);
    gtk_signal_connect(GTK_OBJECT(win), "delete_event", GTK_SIGNAL_FUNC(delete_event), NULL);

    box = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(win), box);

    row = gtk_hbox_new(FALSE, 0);
    side = gtk_vbox_new(FALSE, 4);
    gtk_container_set_border_width(GTK_CONTAINER(side), 2);
    gtk_box_pack_start(GTK_BOX(side), tool_box(), FALSE, FALSE, 0);
    options = gtk_drawing_area_new();
    gtk_drawing_area_size(GTK_DRAWING_AREA(options), OPTIONS_W, OPTIONS_H);
    gtk_widget_set_events(options, GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK);
    gtk_signal_connect(GTK_OBJECT(options), "expose_event", GTK_SIGNAL_FUNC(options_expose), NULL);
    gtk_signal_connect(GTK_OBJECT(options), "button_press_event", GTK_SIGNAL_FUNC(options_press), NULL);
    align = gtk_alignment_new(0.5, 0.0, 0.0, 0.0);
    gtk_container_add(GTK_CONTAINER(align), options);
    gtk_box_pack_start(GTK_BOX(side), align, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), side, FALSE, FALSE, 0);

    view = view_new();
    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled), view);
    gtk_box_pack_start(GTK_BOX(row), scrolled, TRUE, TRUE, 0);

    colours = gtk_drawing_area_new();
    gtk_drawing_area_size(GTK_DRAWING_AREA(colours), USED + PALETTE / 2 * SWATCH, 2 * SWATCH + 2);
    gtk_widget_set_events(colours, GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK);
    gtk_signal_connect(GTK_OBJECT(colours), "expose_event", GTK_SIGNAL_FUNC(colours_expose), NULL);
    gtk_signal_connect(GTK_OBJECT(colours), "button_press_event", GTK_SIGNAL_FUNC(colours_press), NULL);
    align = gtk_alignment_new(0.0, 0.5, 0.0, 0.0);
    gtk_container_set_border_width(GTK_CONTAINER(align), 3);
    gtk_container_add(GTK_CONTAINER(align), colours);

    status = gtk_hbox_new(FALSE, 2);
    said = status_part(status, 0);
    where = status_part(status, 110);
    howbig = status_part(status, 110);
    gtk_label_set_text(GTK_LABEL(said), _(tool_says[tool].hint));

    gtk_box_pack_start(GTK_BOX(box), menus(accel), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), align, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), status, FALSE, FALSE, 0);
    gtk_widget_show_all(box);

    gtk_widget_realize(win);
    icon = gdk_pixmap_create_from_xpm_d(win->window, &icon_mask, NULL, cgpaint_xpm);
    pictures_make();
    gtk_signal_connect(GTK_OBJECT(win), "style_set", GTK_SIGNAL_FUNC(style_set), NULL);
    gtk_widget_grab_focus(view);
    set_title();
}

static void
usage(const gchar *argv0)
{
    printf(_("Usage: %s [OPTION...] [FILE]\n"), argv0);
    printf("\n");
    printf(_("  --help       show this help and exit\n"));
    printf(_("  --version    show the version and exit\n"));
    printf("\n");
    printf(_("The usual GTK options, such as --display, are also understood.\n"));
}

int
main(int argc, char **argv)
{
    gint i;

#ifdef ENABLE_NLS
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    textdomain(GETTEXT_PACKAGE);
#endif
    gtk_set_locale();

    for (i = 1; i < argc; i++)  /* these need no display */
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(argv[0]);
            return 0;
        } else if (!strcmp(argv[i], "--version")) {
            printf("cgpaint %s\n", PACKAGE_VERSION);
            return 0;
        }

    gtk_init(&argc, &argv);
    gtk_window_set_default_icon_name("cgpaint");
    build();
    gtk_widget_show(win);
    if (argc > 1)
        load(argv[1]);
    gtk_main();
    return 0;
}
