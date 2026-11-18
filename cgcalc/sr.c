/*
 * based on xcalc/sr.c from X11R3:
 *
 *      Copyright 1988, Massachusetts Institute of Technology.
 *      XConsortium: sr.c,v 1.10 88/10/22 10:37:58 jim Exp
 *
 * Converted to ANSI C and dropped into gcalc by jwz@jwz.org, 24-Jun-2001.
 *
 * Ported from Xlib to GTK+ and GDK in 2026.
 */

/* Slide Rule */

/*
 * The X version was made of X windows: the frame with the fixed scales,
 * the slide on top of it, a black window behind the slide, the hairline
 * and the box with the readings, each moved and drawn by X. Here they
 * are one drawing area, and the parts are drawn one on another in that
 * order. Making the rule longer made the X window wider, moved so that
 * what was under the pointer stayed there; here the rule is longer than
 * the window, which scrolls instead.
 *
 * The mouse does what it did:
 *
 *   on the slide    button 1  puts the slide's left index under the pointer
 *                   button 2  drags the slide from where it was taken
 *                   button 3  puts the slide's right index under the pointer
 *   elsewhere       button 1  moves the hairline to the pointer
 *                   button 2  makes the rule twice as long
 *                   button 3  makes it half as long again
 *
 * and shift with button 2 or 3 quits.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <gtk/gtk.h>

#include "cgcalc.h"
#include "sr.h"


#define SLIDETOP        26
#define SLIDEHIGH       38
#define WIDTH           800
#define HEIGHT          100
#define START           70
#define END             20
#define LABEL           55
#define MAXWIDTH        32766

#define FOFFSET         3
#define MAJORH          10
#define MIDDH           7
#define MINORH          4
#define HALVES          5
#define FIFTHS          15
#define TENTHS          40
#define LABELFIFTH      41
#define LABELTENTHS     200
#define LABELSPACE      20

#define LSCALEH         2
#define ASCALEH         16
#define BSCALEH         0
#define CISCALEH        14
#define CSCALEH         28
#define DSCALEH         66
#define DISCALEH        84

/* the box with the readings, at the left of the window */
#define DISPWIDTH       (START-END)

static GtkWidget *theWindow, *area, *hscroll;
static GtkAdjustment *hadj;
static GdkFont  *kfont;
static GdkPixmap *slidePix = NULL;
static GdkPixmap *framePix = NULL;
static GdkPixmap *backPix = NULL;       /* where the window is put together */
static GdkGC    *sgc, *cgc;             /* black on white, white on black */
static GdkGC    *dgc;                   /* black, kept to the readings' box */
static int      scalelen,width,fheight,foffset;
static int      xo,hx,xm;
static double   logtenoftwo;
static int      view_width = WIDTH;     /* how much of the rule is shown */
static int      view_x = 0;             /* and from where */
static int      zoom = 0;               /* the rule is view_width << zoom long */
static int      back_width, back_height;

static enum { DRAG_NONE, DRAG_SLIDE, DRAG_FRAME } drag = DRAG_NONE;

#define KFONT "-*-helvetica-medium-r-normal--10-*-*-*-*-*-*-*"


static void rescale(int w, int anchor);
static void drawframe(void);
static void doscale(GdkDrawable *win, int high, int offset, int len, int topp);
static void dotenths(GdkDrawable *win, int high, int offset, int len,
                     const char *str, int topp);
static void drawslide(void);
static void drawnums(GdkDrawable *d);
static void redraw(void);


#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))


/***********************************/

/* the view has moved or changed size: the scrollbar follows */
static void
update_scrollbar (void)
{
    view_x = max (0, min (view_x, width - view_width));

    hadj->lower = 0;
    hadj->upper = width;
    hadj->page_size = view_width;
    hadj->step_increment = 10;
    hadj->page_increment = view_width * 9 / 10;
    hadj->value = view_x;
    gtk_adjustment_changed (hadj);
    gtk_adjustment_value_changed (hadj);

    if (zoom > 0)
        gtk_widget_show (hscroll);
    else
        gtk_widget_hide (hscroll);
}

/* the rule becomes w long, with what was at anchor in the window left
 * there */
static void
rescale(int w, int anchor)
{
    int s = anchor + view_x;

    if (width == w)
        return;

    hx = (hx * w) / width;
    xo = (xo * w) / width;
    view_x = (int) (((double) s * w) / width) - anchor;
    width = w;
    scalelen = width - START - END;

    if (framePix)
      gdk_pixmap_unref (framePix);
    framePix = NULL;
    if (slidePix)
      gdk_pixmap_unref (slidePix);
    slidePix = NULL;

    update_scrollbar ();
    redraw ();
}

static void
drawmark(GdkDrawable *win, int x, int y, int height, int topp)
{
    gdk_draw_line(win, sgc, x, (topp?y:y+MAJORH-height),
                  x, height + (topp?y:y+MAJORH-height));
}

static void
dolabel(GdkDrawable *win, int x, int y, const char *str, int topp, int majorp G_GNUC_UNUSED)
{
    gdk_draw_string(win, kfont, sgc, x + 2,
                    (topp?y+FOFFSET+foffset:y+MAJORH-fheight+foffset),
                    str);
}

static void
drawframe(void)
{
    int         i,x,j;
    char        str[16];
    int midpt = scalelen/2;

    if (framePix == NULL)
      framePix = gdk_pixmap_new (area->window, width, HEIGHT, -1);
    gdk_draw_rectangle(framePix, cgc, TRUE, 0, 0, width, HEIGHT);
    gdk_draw_string(framePix, kfont, sgc, LABEL, LSCALEH+foffset, "L");
    for (i = 0; i <= 10; i++) {
        g_snprintf(str, sizeof str, "%d", i);
        x = START+(i*scalelen)/10;
        dolabel(framePix, x, LSCALEH, str, 0, 1);
        drawmark(framePix, x, LSCALEH, MAJORH, 0);
        for (j = 1; j < 10; j++)
          drawmark(framePix, x+(j*scalelen)/100, LSCALEH, (j==5?MIDDH:MINORH),
                   0);
    }
    gdk_draw_string(framePix, kfont, sgc, LABEL, ASCALEH+foffset, "A");
    doscale(framePix, ASCALEH, START, midpt, 0);
    doscale(framePix, ASCALEH, START + midpt, scalelen - midpt, 0);
    gdk_draw_string(framePix, kfont, sgc, LABEL, DSCALEH+foffset, "D");
    doscale(framePix, DSCALEH, START, scalelen, 1);
    gdk_draw_string(framePix, kfont, sgc, LABEL, DISCALEH+foffset, "DI");
    for (i = 1; i <= 10; i++) {
        x = START + scalelen * (1 - log10((float) i)) + 0.5;
        g_snprintf(str, sizeof str, "%d", (i==10?1:i));
        dolabel(framePix, x, DISCALEH, str, 1, 1);
        drawmark(framePix, x, DISCALEH, MAJORH, 1);
    }
}

static void
doscale(GdkDrawable *win, int high, int offset, int len, int topp)
{
  int   i;
  int   xs[11];
  char  str[16];

  xs[0] = offset - 100;
  for (i = 1; i <= 10; i++)
    xs[i] = offset + len * log10((float) i) + 0.5;

  for (i = 1; i < 10; i++) {
      if (xs[i] > xs[i-1] + LABELSPACE) {
          g_snprintf(str, sizeof str, "%d", (i==10?1:i));
          dolabel(win, xs[i], high, str, topp, 1);
          drawmark(win, xs[i], high, MAJORH, topp);
      } else
        drawmark(win, xs[i], high, MIDDH, topp);

      g_snprintf(str, sizeof str, "%d.", i);
      dotenths(win, high, xs[i], xs[i+1]-xs[i], str, topp);
  }
  dolabel(win, xs[i], high, "1", topp, 1);
  drawmark(win, xs[i], high, MAJORH, topp);
}

static void
dotenths(GdkDrawable *win, int high, int offset, int len, const char *str,
         int topp)
{
    int i;
    int xs[11];
    char nstr[32];

    for (i = 0; i <= 10; i++ )
      xs[i] = offset +
        len * log10((float) 1.0 + ((float) i) / 10.0) / logtenoftwo;

    if (len < HALVES)
      return;
    if (len < FIFTHS) {
        drawmark(win, xs[5], high, MINORH, topp);
        return;
    }
    if (len < TENTHS) {
        for (i = 0; i < 10; i += 2)
          drawmark(win, xs[i], high, MINORH, topp);
        return;
    }
    if (len < LABELFIFTH) {
        for (i = 0; i < 10; i++) {
            drawmark(win, xs[i], high, (i==5?MIDDH:MINORH), topp);
            dotenths(win, high, xs[i], xs[i+1]-xs[i], "", topp);
        }
        return;
    }
    if (len < LABELTENTHS) {
        for (i = 0; i < 10; i++) {
            if (i == 5) {
                g_snprintf(nstr, sizeof nstr, "%s%d", str, 5);
                dolabel(win, xs[i], high, nstr, topp, 0);
            }
            drawmark(win, xs[i], high, (i==5?MAJORH:MIDDH), topp);
            dotenths(win, high, xs[i], xs[i+1]-xs[i], "", topp);
        }
    } else {
        for (i = 0; i < 10; i++) {
            g_snprintf(nstr, sizeof nstr, "%s%d", str, i);
            if (i > 0) {
                dolabel(win, xs[i], high, nstr, topp, 0);
                drawmark(win, xs[i], high, MAJORH, topp);
            }
            dotenths(win, high, xs[i], xs[i+1]-xs[i], nstr, topp);
        }
    }
}


static void
drawslide(void)
{
    int         i,x;
    char        str[16];
    int         midpt = scalelen/2;

    if (slidePix == NULL)
      slidePix = gdk_pixmap_new (area->window, width, SLIDEHIGH, -1);
    gdk_draw_rectangle(slidePix, cgc, TRUE, 0, 0, width, SLIDEHIGH);
    gdk_draw_string(slidePix, kfont, sgc, LABEL, BSCALEH+foffset, "B");
    doscale(slidePix, BSCALEH, START, midpt, 1);
    doscale(slidePix, BSCALEH, START + midpt, scalelen - midpt, 1);
    gdk_draw_string(slidePix, kfont, sgc, LABEL, CISCALEH+foffset, "CI");
    for (i = 1; i <= 10; i++) {
        x = START + scalelen * (1 - log10((float) i)) + 0.5;
        g_snprintf(str, sizeof str, "%d", (i==10?1:i));
        dolabel(slidePix, x, CISCALEH, str, 1, 1);
        drawmark(slidePix, x, CISCALEH, MAJORH, 1);
    }
    gdk_draw_string(slidePix, kfont, sgc, LABEL, CSCALEH+foffset, "C");
    doscale(slidePix, CSCALEH, START, scalelen, 0);
}

/* the part of a pixmap wide as the rule that falls in the window */
static void
copy_shown (GdkDrawable *dest, GdkPixmap *src, int x, int y, int h)
{
    int from = max (0, x - view_x);
    int to = min (view_width, x - view_x + width);

    if (to > from)
      gdk_draw_pixmap (dest, sgc, src, from - (x - view_x), 0,
                       from, y, to - from, h);
}

static void
drawnums(GdkDrawable *d)
{
    char        str[64];
    double      x = ((double) (hx - START))/((double) scalelen);
    double      xs = ((double) (hx - START - xo))/((double) scalelen);

    /* the box, which was a window with a border of its own */
    gdk_draw_rectangle(d, cgc, TRUE, 1, 1, DISPWIDTH, HEIGHT);
    gdk_draw_rectangle(d, sgc, FALSE, 0, 0, DISPWIDTH + 1, HEIGHT + 1);

    g_snprintf(str, sizeof str, "%5f", 10. * x);
    gdk_draw_string(d, kfont, dgc, 6, 1+LSCALEH+foffset, str);
    g_snprintf(str, sizeof str, "%5f", pow(100., x));
    gdk_draw_string(d, kfont, dgc, 6, 1+ASCALEH+foffset, str);
    g_snprintf(str, sizeof str, "%5f", pow(100., xs));
    gdk_draw_string(d, kfont, dgc, 6, 1+SLIDETOP+foffset + BSCALEH, str);
    g_snprintf(str, sizeof str, "%5f", pow(10., 1. - xs));
    gdk_draw_string(d, kfont, dgc, 6, 1+SLIDETOP+foffset + CISCALEH, str);
    g_snprintf(str, sizeof str, "%5f", pow(10., xs));
    gdk_draw_string(d, kfont, dgc, 6, 1+SLIDETOP+foffset + CSCALEH, str);
    g_snprintf(str, sizeof str, "%5f", pow(10., x));
    gdk_draw_string(d, kfont, dgc, 6, 1+DSCALEH+foffset, str);
    g_snprintf(str, sizeof str, "%5f", pow(10., 1. - x));
    gdk_draw_string(d, kfont, dgc, 6, 1+DISCALEH+foffset, str);
}

/* puts the window together and shows it */
static void
redraw(void)
{
    int w, h;

    if (!GTK_WIDGET_REALIZED (area))
        return;

    gdk_window_get_size (area->window, &w, &h);
    if (backPix == NULL || back_width != w || back_height != h) {
        if (backPix)
          gdk_pixmap_unref (backPix);
        backPix = gdk_pixmap_new (area->window, w, h, -1);
        back_width = w;
        back_height = h;
    }
    if (framePix == NULL)
      drawframe();
    if (slidePix == NULL)
      drawslide();

    /* the frame, and below it the window's white */
    gdk_draw_rectangle (backPix, cgc, TRUE, 0, 0, w, h);
    copy_shown (backPix, framePix, 0, 0, HEIGHT);

    /* the black behind the slide, and the slide in its black border */
    gdk_draw_rectangle (backPix, sgc, TRUE, 0, SLIDETOP, w, SLIDEHIGH+2);
    copy_shown (backPix, slidePix, xo, SLIDETOP+1, SLIDEHIGH);

    /* the hairline */
    gdk_draw_line (backPix, sgc, hx - view_x, 0, hx - view_x, HEIGHT - 1);

    /* the readings, which stay where they are when the rule scrolls */
    drawnums (backPix);

    gdk_draw_pixmap (area->window, sgc, backPix, 0, 0, 0, 0, w, h);
}


/**************** Events *****************/

static gint
area_expose (GtkWidget *widget G_GNUC_UNUSED, GdkEventExpose *event G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
    redraw ();
    return TRUE;
}

static void
area_realize (GtkWidget *widget, gpointer data G_GNUC_UNUSED)
{
    GdkCursor *arrow;

    sgc = gdk_gc_new (widget->window);
    gdk_gc_set_foreground (sgc, &widget->style->black);
    gdk_gc_set_background (sgc, &widget->style->white);
    cgc = gdk_gc_new (widget->window);
    gdk_gc_set_foreground (cgc, &widget->style->white);
    gdk_gc_set_background (cgc, &widget->style->black);
    dgc = gdk_gc_new (widget->window);
    gdk_gc_set_foreground (dgc, &widget->style->black);
    {
        GdkRectangle box = { 1, 1, DISPWIDTH, HEIGHT };
        gdk_gc_set_clip_rectangle (dgc, &box);
    }

    kfont = gdk_font_load (KFONT);
    if (kfont == NULL)
      kfont = gdk_font_ref (widget->style->font);
    fheight = kfont->ascent + FOFFSET;
    foffset = kfont->ascent;

    arrow = gdk_cursor_new (GDK_HAND2);
    gdk_window_set_cursor (widget->window, arrow);
    gdk_cursor_destroy (arrow);
}

static void
area_size_allocate (GtkWidget *widget G_GNUC_UNUSED, GtkAllocation *allocation,
                    gpointer data G_GNUC_UNUSED)
{
    int w;

    if (allocation->width == view_width)
        return;

    /* the rule fits the window, or is so many times longer */
    view_width = allocation->width;
    w = view_width << zoom;
    while (zoom > 0 && w >= MAXWIDTH)
        w = view_width << --zoom;
    rescale (w, 0);
    update_scrollbar ();
}

static void
scrolled (GtkAdjustment *adj, gpointer data G_GNUC_UNUSED)
{
    if ((int) adj->value != view_x) {
        view_x = (int) adj->value;
        redraw ();
    }
}

/* where the slide is, and the black behind it */
static gboolean
on_slide (int x, int y)
{
    return y >= SLIDETOP && y < SLIDETOP + SLIDEHIGH + 2 &&
           !(x <= DISPWIDTH + 1 && y <= HEIGHT + 1);
}

static gint
area_button_press (GtkWidget *widget G_GNUC_UNUSED, GdkEventButton *event, gpointer data G_GNUC_UNUSED)
{
    int x = (int) event->x;
    int y = (int) event->y;
    int s = x + view_x;         /* where on the rule */

    if (event->type != GDK_BUTTON_PRESS)
        return TRUE;

    if ((event->button == 2 || event->button == 3) &&
        (event->state & GDK_SHIFT_MASK)) {
        gtk_main_quit ();
        return TRUE;
    }

    if (on_slide (x, y)) {
        switch (event->button) {
        case 1:
            xo = s - START;
            drag = DRAG_SLIDE;
            break;
        case 2:
            if (s >= xo && s < xo + width) {
                xm = s - xo;
                drag = DRAG_SLIDE;
            }
            break;
        case 3:
            xo = s + END - width;
            drag = DRAG_SLIDE;
            break;
        }
        redraw ();
    } else {
        switch (event->button) {
        case 1:
            hx = s;
            drag = DRAG_FRAME;
            redraw ();
            break;
        case 2:
            if (width*2 < MAXWIDTH) {
                zoom++;
                rescale (width*2, x);
            }
            break;
        case 3:
            if (zoom > 0) {
                zoom--;
                rescale (width/2, x);
            }
            break;
        }
    }

    return TRUE;
}

static gint
area_motion (GtkWidget *widget G_GNUC_UNUSED, GdkEventMotion *event, gpointer data G_GNUC_UNUSED)
{
    int s = (int) event->x + view_x;
    int newx;

    if (drag == DRAG_SLIDE) {
        if (event->state & GDK_BUTTON1_MASK)
          newx = s - START;
        else if (event->state & GDK_BUTTON2_MASK)
          newx = s - xm;
        else if (event->state & GDK_BUTTON3_MASK)
          newx = s + END - width;
        else
          return TRUE;
        if (newx != xo) {
            xo = newx;
            redraw ();
        }
    } else if (drag == DRAG_FRAME && (event->state & GDK_BUTTON1_MASK)) {
        if (s != hx) {
            hx = s;
            redraw ();
        }
    }

    return TRUE;
}

static gint
area_button_release (GtkWidget *widget G_GNUC_UNUSED, GdkEventButton *event, gpointer data G_GNUC_UNUSED)
{
    guint held = event->state & (GDK_BUTTON1_MASK | GDK_BUTTON2_MASK |
                                 GDK_BUTTON3_MASK);

    /* the state is from before the release: is it the last button? */
    if (held == (guint) GDK_BUTTON1_MASK << (event->button - 1))
        drag = DRAG_NONE;

    return TRUE;
}

static gint
delete_cb (GtkWidget *widget G_GNUC_UNUSED, GdkEventAny *event G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
    gtk_main_quit ();
    return TRUE;
}

void
run_slide_rule (void)
{
    GtkWidget *vbox;

    width = WIDTH;
    scalelen = width - START - END;
    xo = 0;
    hx = width/2;
    logtenoftwo = log10(2.0);

    theWindow = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title (GTK_WINDOW (theWindow), _("Slide Rule"));
    gtk_window_set_wmclass (GTK_WINDOW (theWindow), "cgcalc", "Cgcalc");
    gtk_window_set_policy (GTK_WINDOW (theWindow), FALSE, TRUE, FALSE);
    gtk_signal_connect (GTK_OBJECT (theWindow), "delete_event",
                        GTK_SIGNAL_FUNC (delete_cb), NULL);

    vbox = gtk_vbox_new (FALSE, 0);
    gtk_container_add (GTK_CONTAINER (theWindow), vbox);
    gtk_widget_show (vbox);

    area = gtk_drawing_area_new ();
    gtk_drawing_area_size (GTK_DRAWING_AREA (area), WIDTH, HEIGHT);
    gtk_widget_set_events (area, GDK_EXPOSURE_MASK |
                           GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                           GDK_BUTTON1_MOTION_MASK | GDK_BUTTON2_MOTION_MASK |
                           GDK_BUTTON3_MOTION_MASK);
    gtk_signal_connect (GTK_OBJECT (area), "realize",
                        GTK_SIGNAL_FUNC (area_realize), NULL);
    gtk_signal_connect (GTK_OBJECT (area), "size_allocate",
                        GTK_SIGNAL_FUNC (area_size_allocate), NULL);
    gtk_signal_connect (GTK_OBJECT (area), "expose_event",
                        GTK_SIGNAL_FUNC (area_expose), NULL);
    gtk_signal_connect (GTK_OBJECT (area), "button_press_event",
                        GTK_SIGNAL_FUNC (area_button_press), NULL);
    gtk_signal_connect (GTK_OBJECT (area), "motion_notify_event",
                        GTK_SIGNAL_FUNC (area_motion), NULL);
    gtk_signal_connect (GTK_OBJECT (area), "button_release_event",
                        GTK_SIGNAL_FUNC (area_button_release), NULL);
    gtk_box_pack_start (GTK_BOX (vbox), area, TRUE, TRUE, 0);
    gtk_widget_show (area);

    hadj = GTK_ADJUSTMENT (gtk_adjustment_new (0, 0, width, 10,
                                               view_width, view_width));
    gtk_signal_connect (GTK_OBJECT (hadj), "value_changed",
                        GTK_SIGNAL_FUNC (scrolled), NULL);
    hscroll = gtk_hscrollbar_new (hadj);
    gtk_box_pack_start (GTK_BOX (vbox), hscroll, FALSE, FALSE, 0);

    cgcalc_set_icon (theWindow);
    gtk_widget_show (theWindow);

    gtk_main ();
}
