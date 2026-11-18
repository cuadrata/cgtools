/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1997, 1998, 1999, 2000 Free Software Foundation
 * All rights reserved.
 *
 * This file was part of the Gnome Library.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library. If not, see
 * <https://www.gnu.org/licenses/>.
 */
/* GtkCalc - double precision simple calculator widget
 *
 * Author: George Lebl <jirka@5z.com>
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <locale.h>
#include <errno.h> /* errno */
#include <signal.h> /* signal() */
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "cgcalc.h"
#include "gtkcalc.h"

/* One character of the display: GNOME drew it from a picture of the
 * digits, here it is drawn in segments. */
#define FONT_WIDTH 20
#define FONT_HEIGHT 30
#define DISPLAY_LEN 13

/* The programmer's display is wide enough for a 64-bit word in octal
 * with the memory mark beside it; only binary can overflow it. */
#define PROG_DISPLAY_LEN 24

/* room for a 64-bit word in binary, a sign and the end */
#define RESULT_LEN 72

struct _GtkCalcPrivate {
        GtkWidget *display;
        GdkGC *lit_gc;          /* what the digits are drawn in */

        GtkWidget *table;       /* the keypad */
        GtkWidget *invert_button;
        GtkWidget *drg_button;

        /* the programmer's keypad */
        GtkWidget *bits;        /* every bit of the word, to read and click */
        GtkWidget *base_button;
        GtkWidget *width_button;
        GtkWidget *digit_buttons[16];

        GList *stack;
        GtkAccelGroup *accel;

        gdouble result;
        gdouble memory;

        /* the programmer's numbers: words of width bits, kept masked */
        guint64 iresult;
        guint64 imemory;
        guint64 entry;          /* how much has been typed, without sign */
        guint width;

        gchar result_string[RESULT_LEN];

        GtkCalcLayout layout;
        GtkCalcBase base;
        GtkCalcMode mode : 2;

        guint is_signed : 1;    /* a word is two's complement, not unsigned */
        guint entry_neg : 1;    /* what is being typed is negative */

        guint add_digit : 1;    /*add a digit instead of starting a new
                                  number*/
        guint error : 1;
        guint invert : 1;
};

typedef enum {
        CALCULATOR_NUMBER,
        CALCULATOR_FUNCTION,
        CALCULATOR_PARENTHESIS
} CalculatorActionType;

typedef gdouble (*MathFunction1) (gdouble);
typedef gdouble (*MathFunction2) (gdouble, gdouble);

/* the programmer's operations, on words as unsigned 64-bit patterns;
 * their results are masked to the word size afterwards */
typedef guint64 (*IntFunction1) (GtkCalc *, guint64);
typedef guint64 (*IntFunction2) (GtkCalc *, guint64, guint64);

typedef struct _CalculatorStack CalculatorStack;
struct _CalculatorStack {
        CalculatorActionType type;
        MathFunction2 func;     /* a function, on the scientific keypad */
        IntFunction2 ifunc;     /* a function, on the programmer's */
        union {
                gdouble number;
                guint64 inum;
        } d;
};




static void gtk_calc_class_init (GtkCalcClass   *class);
static void gtk_calc_init            (gpointer object, gpointer klass);
static void gtk_calc_destroy    (GtkObject      *object);
static void gtk_calc_finalize   (GtkObject      *object);
static GtkVBoxClass *parent_class;

typedef struct _CalculatorButton CalculatorButton;

/* what a button does when clicked */
typedef void (*CalculatorFunc) (GtkWidget *w, gpointer data);

struct _CalculatorButton {
        const char *name;
        CalculatorFunc signal_func;
        MathFunction1 func1;    /* for simple_func() and negate_val() */
        MathFunction1 invfunc1;
        MathFunction2 func2;    /* for math_func() */
        IntFunction1 ifunc1;    /* for int_func() and negate_val() */
        IntFunction2 ifunc2;    /* for math_func() */
        const char *digit;      /* what add_digit() adds, if not the name */
        gboolean convert_to_rad;
        gboolean toggle;        /* a toggle button, signal_func its "toggled" */
        guint keys[10]; /*key shortcuts, 0 terminated list,
                          make sure to increase the number if more is needed*/
};

typedef void (*GtkCalcResultChangedSignal) (GtkObject * object,
                                                    gdouble result,
                                                    gpointer data);


enum {
        RESULT_CHANGED_SIGNAL,
        LAST_SIGNAL
};

static guint gtk_calc_signals[LAST_SIGNAL] = {0};


static void
gtk_calc_marshal_signal_result_changed (GtkObject * object,
                                           GtkSignalFunc func,
                                           gpointer func_data,
                                           GtkArg * args)
{
        GtkCalcResultChangedSignal rfunc;

        rfunc = (GtkCalcResultChangedSignal) func;

        (*rfunc) (object, GTK_VALUE_DOUBLE (args[0]),
                  func_data);
}

GtkType
gtk_calc_get_type (void)
{
        static GtkType calculator_type = 0;

        if (!calculator_type) {
                GtkTypeInfo calculator_info = {
                        "GtkCalc",
                        sizeof (GtkCalc),
                        sizeof (GtkCalcClass),
                        (GtkClassInitFunc)  gtk_calc_class_init,
                        gtk_calc_init,
                        NULL,
                        NULL,
                        NULL
                };

                calculator_type = gtk_type_unique (gtk_vbox_get_type (),
                                                   &calculator_info);
        }

        return calculator_type;
}


static void
gtk_calc_class_init (GtkCalcClass *class)
{
        GtkObjectClass *object_class;

        object_class = (GtkObjectClass *) class;
        parent_class = gtk_type_class (gtk_vbox_get_type ());
        object_class->destroy = gtk_calc_destroy;
        object_class->finalize = gtk_calc_finalize;

        gtk_calc_signals[RESULT_CHANGED_SIGNAL] =
                gtk_signal_new("result_changed",
                               GTK_RUN_LAST,
                               object_class->type,
                               GTK_SIGNAL_OFFSET(GtkCalcClass,
                                                 result_changed),
                               gtk_calc_marshal_signal_result_changed,
                               GTK_TYPE_NONE,
                               1,
                               GTK_TYPE_DOUBLE);

        class->result_changed = NULL;
        gtk_object_class_add_signals (object_class, gtk_calc_signals, LAST_SIGNAL);
}

static void
stack_pop(GList **stack)
{
        CalculatorStack *s;
        GList *p;

        g_return_if_fail(stack);

        if(*stack == NULL) {
                g_warning("Stack underflow!");
                return;
        }

        s = (*stack)->data;
        p = (*stack)->next;
        g_list_free_1(*stack);
        if(p) p->prev = NULL;
        *stack = p;
        g_free(s);
}

static void
do_error(GtkCalc *gc)
{
        gc->_priv->error = TRUE;
        strcpy(gc->_priv->result_string,"e");
        gtk_widget_queue_draw (gc->_priv->display);
}

/*we handle sigfpe's so that we can find all the errors*/
static void
sigfpe_handler(int type G_GNUC_UNUSED)
{
        /*most likely, but we don't really care what the value is*/
        errno = ERANGE;
}

/*
 * The programmer's words. A word is kept as an unsigned 64-bit
 * pattern masked to the word size; whether it is signed matters only
 * to how it is shown in decimal, to division and to shifting right.
 */
static guint64
word_mask(GtkCalc *gc)
{
        if(gc->_priv->width >= 64)
                return ~(guint64)0;
        return ((guint64)1 << gc->_priv->width) - 1;
}

static gboolean
word_negative(GtkCalc *gc, guint64 v)
{
        return gc->_priv->is_signed &&
                ((v >> (gc->_priv->width - 1)) & 1);
}

/* the word's pattern, sign extended to 64 bits if it is negative */
static guint64
sign_extend(GtkCalc *gc, guint64 v)
{
        v &= word_mask(gc);
        if(word_negative(gc, v))
                v |= ~word_mask(gc);
        return v;
}

static guint
base_radix(GtkCalcBase base)
{
        switch(base) {
        case GTK_CALC_BASE_HEX: return 16;
        case GTK_CALC_BASE_OCT: return 8;
        case GTK_CALC_BASE_BIN: return 2;
        default:                return 10;
        }
}

/* the word as the display shows it: a sign or a blank, then digits */
static void
format_word(GtkCalc *gc, guint64 v, gchar *buf, gsize size)
{
        static const char digits[] = "0123456789ABCDEF";
        guint radix = base_radix(gc->_priv->base);
        gboolean neg = FALSE;
        gchar tmp[RESULT_LEN];
        gint i = sizeof(tmp);

        v &= word_mask(gc);
        if(gc->_priv->base == GTK_CALC_BASE_DEC && word_negative(gc, v)) {
                neg = TRUE;
                v = (guint64)0 - sign_extend(gc, v);
        }

        tmp[--i] = '\0';
        do {
                tmp[--i] = digits[v % radix];
                v /= radix;
        } while(v);
        tmp[--i] = neg ? '-' : ' ';

        g_snprintf(buf, size, "%s", tmp + i);
}

/* a number from the scientific keypad, cut to a whole word */
static guint64
double_to_word(GtkCalc *gc, gdouble v)
{
        guint64 r;

        if(!isfinite(v))
                return 0;
        v = trunc(v);
        if(v >= 18446744073709551616.0)
                r = ~(guint64)0;
        else if(v >= 0)
                r = (guint64)v;
        else if(v <= -9223372036854775808.0)
                r = (guint64)1 << 63;
        else
                r = (guint64)0 - (guint64)(-v);

        return r & word_mask(gc);
}

static gdouble
word_to_double(GtkCalc *gc, guint64 v)
{
        v &= word_mask(gc);
        if(word_negative(gc, v))
                return -(gdouble)((guint64)0 - sign_extend(gc, v));
        return (gdouble)v;
}

static void
redisplay(GtkCalc *gc)
{
        gtk_widget_queue_draw(gc->_priv->display);
        if(gc->_priv->layout == GTK_CALC_PROGRAMMER)
                gtk_widget_queue_draw(gc->_priv->bits);
}

static void
reduce_stack(GtkCalc *gc)
{
        CalculatorStack *stack;
        CalculatorStack *op;
        CalculatorStack *first;
        GList *list;

        if(!gc->_priv->stack)
                return;

        stack = gc->_priv->stack->data;
        if(stack->type!=CALCULATOR_NUMBER)
                return;

        list=g_list_next(gc->_priv->stack);
        if(!list)
                return;

        op = list->data;
        if(op->type==CALCULATOR_PARENTHESIS)
                return;
        if(op->type!=CALCULATOR_FUNCTION) {
                g_warning("Corrupt GtkCalc stack!");
                return;
        }

        list=g_list_next(list);
        if(!list) {
                g_warning("Corrupt GtkCalc stack!");
                return;
        }

        first = list->data;
        if(first->type!=CALCULATOR_NUMBER) {
                g_warning("Corrupt GtkCalc stack!");
                return;
        }

        errno = 0;

        if(gc->_priv->layout == GTK_CALC_PROGRAMMER) {
                IntFunction2 ifunc = op->ifunc;
                guint64 second = stack->d.inum;

                stack_pop(&gc->_priv->stack);
                stack_pop(&gc->_priv->stack);

                /* nothing here traps: division by zero is looked for */
                first->d.inum = (*ifunc)(gc, first->d.inum, second) &
                        word_mask(gc);
        } else {
                MathFunction2 func = op->func;
                gdouble second = stack->d.number;
                void (*old)(int);

                stack_pop(&gc->_priv->stack);
                stack_pop(&gc->_priv->stack);

                old = signal(SIGFPE,sigfpe_handler);
                first->d.number = (*func)(first->d.number,second);
                signal(SIGFPE,old);

                if(!isfinite(first->d.number))
                        errno = ERANGE;
        }

        if(errno>0) {
                errno = 0;
                do_error(gc);
        }
}

/* Move these up for find_precedence(). */
static gdouble
c_add(gdouble arg1, gdouble arg2)
{
        return arg1+arg2;
}

static gdouble
c_sub(gdouble arg1, gdouble arg2)
{
        return arg1-arg2;
}

static gdouble
c_mul(gdouble arg1, gdouble arg2)
{
        return arg1*arg2;
}

static gdouble
c_div(gdouble arg1, gdouble arg2)
{
        if(arg2==0) {
                errno=ERANGE;
                return 0;
        }
        return arg1/arg2;
}

/* The programmer's operations. Only division, the remainder and the
 * right shift need to know whether a word is signed. */
static guint64
i_add(GtkCalc *gc G_GNUC_UNUSED, guint64 a, guint64 b)
{
        return a + b;
}

static guint64
i_sub(GtkCalc *gc G_GNUC_UNUSED, guint64 a, guint64 b)
{
        return a - b;
}

static guint64
i_mul(GtkCalc *gc G_GNUC_UNUSED, guint64 a, guint64 b)
{
        return a * b;
}

/* Division, or with rem the remainder. As in C, the quotient is
 * truncated and the remainder has the sign of the dividend; the most
 * negative word divided by -1 wraps round to itself. */
static guint64
i_divide(GtkCalc *gc, guint64 a, guint64 b, gboolean rem)
{
        guint64 mask = word_mask(gc);
        gboolean neg_a, neg_b;
        guint64 r;

        a &= mask;
        b &= mask;
        if(b == 0) {
                errno = ERANGE;
                return 0;
        }

        neg_a = word_negative(gc, a);
        neg_b = word_negative(gc, b);
        if(neg_a)
                a = (guint64)0 - sign_extend(gc, a);
        if(neg_b)
                b = (guint64)0 - sign_extend(gc, b);

        if(rem) {
                r = a % b;
                return neg_a ? (guint64)0 - r : r;
        }
        r = a / b;
        return neg_a != neg_b ? (guint64)0 - r : r;
}

static guint64
i_div(GtkCalc *gc, guint64 a, guint64 b)
{
        return i_divide(gc, a, b, FALSE);
}

static guint64
i_mod(GtkCalc *gc, guint64 a, guint64 b)
{
        return i_divide(gc, a, b, TRUE);
}

static guint64
i_and(GtkCalc *gc G_GNUC_UNUSED, guint64 a, guint64 b)
{
        return a & b;
}

static guint64
i_or(GtkCalc *gc G_GNUC_UNUSED, guint64 a, guint64 b)
{
        return a | b;
}

static guint64
i_xor(GtkCalc *gc G_GNUC_UNUSED, guint64 a, guint64 b)
{
        return a ^ b;
}

/* A shift by the word size or more shifts everything out. */
static guint64
i_shl(GtkCalc *gc, guint64 a, guint64 b)
{
        b &= word_mask(gc);
        if(b >= gc->_priv->width)
                return 0;
        return a << b;
}

/* Arithmetic if the word is signed, logical if not. */
static guint64
i_shr(GtkCalc *gc, guint64 a, guint64 b)
{
        b &= word_mask(gc);
        if(b >= gc->_priv->width)
                b = gc->_priv->width - 1 + !word_negative(gc, a);
        if(b >= 64)
                return 0;
        if(word_negative(gc, a))
                return ~(~sign_extend(gc, a) >> b);
        return (a & word_mask(gc)) >> b;
}

static guint64
i_not(GtkCalc *gc G_GNUC_UNUSED, guint64 a)
{
        return ~a;
}

static guint64
i_neg(GtkCalc *gc G_GNUC_UNUSED, guint64 a)
{
        return (guint64)0 - a;
}

static int
find_precedence(MathFunction2 f)
{
        if ( f == NULL || f == c_add || f == c_sub)
                return 0;
        else if ( f == c_mul || f == c_div )
                return 1;
        else
                return 2;
}

/* C's precedence: | below ^ below & below the shifts, then + and -,
 * then *, / and MOD */
static int
find_int_precedence(IntFunction2 f)
{
        if(f == NULL || f == i_or)
                return 0;
        else if(f == i_xor)
                return 1;
        else if(f == i_and)
                return 2;
        else if(f == i_shl || f == i_shr)
                return 3;
        else if(f == i_add || f == i_sub)
                return 4;
        else
                return 5;
}

static void
reduce_stack_prec(GtkCalc *gc, MathFunction2 func, IntFunction2 ifunc)
{
        CalculatorStack *stack;
        GList *list;

        stack = gc->_priv->stack->data;
        if ( stack->type != CALCULATOR_NUMBER )
                return;

        list = g_list_next(gc->_priv->stack);
        if (!list)
                return;

        stack = list->data;
        if ( stack->type == CALCULATOR_PARENTHESIS )
                return;

        if ( gc->_priv->layout == GTK_CALC_PROGRAMMER ?
             find_int_precedence(ifunc) <= find_int_precedence(stack->ifunc) :
             find_precedence(func) <= find_precedence(stack->func) ) {
                reduce_stack(gc);
                if (gc->_priv->error)
                        return;
                reduce_stack_prec(gc,func,ifunc);
        }

        return;
}

static void
push_input(GtkCalc *gc)
{
        if(gc->_priv->add_digit) {
                CalculatorStack *stack;
                stack = g_new0(CalculatorStack,1);
                stack->type = CALCULATOR_NUMBER;
                if(gc->_priv->layout == GTK_CALC_PROGRAMMER)
                        stack->d.inum = gc->_priv->iresult;
                else
                        stack->d.number = gc->_priv->result;
                gc->_priv->stack = g_list_prepend(gc->_priv->stack,stack);
                gc->_priv->add_digit = FALSE;
        }
}

static void
set_result(GtkCalc *gc)
{
        CalculatorStack *stack;
        gchar buf[80];
        gchar format[20];
        gint i;
        char *old_locale;

        g_return_if_fail(gc!=NULL);

        if(!gc->_priv->stack)
                return;

        stack = gc->_priv->stack->data;
        if(stack->type!=CALCULATOR_NUMBER)
                return;

        if(gc->_priv->layout == GTK_CALC_PROGRAMMER) {
                gc->_priv->iresult = stack->d.inum & word_mask(gc);
                format_word(gc, gc->_priv->iresult,
                            gc->_priv->result_string,
                            sizeof(gc->_priv->result_string));
                redisplay(gc);
                gtk_signal_emit(GTK_OBJECT(gc),
                                gtk_calc_signals[RESULT_CHANGED_SIGNAL],
                                word_to_double(gc, gc->_priv->iresult));
                return;
        }

        gc->_priv->result = stack->d.number;

        /* make sure put values in a consistent manner */
        /* XXX: perhaps we can make sure the calculator works on all locales,
         * but currently it will lose precision if we don't do this */
        old_locale = g_strdup (setlocale (LC_NUMERIC, NULL));
        setlocale (LC_NUMERIC, "C");
        for (i = 12; i > 0; i--) {
                g_snprintf (format, sizeof (format), "%c .%dg", '%', i);
                g_snprintf (buf, sizeof (buf), format, gc->_priv->result);
                if (strlen (buf) <= 12)
                        break;
        }
        setlocale (LC_NUMERIC, old_locale);
        g_free (old_locale);
        g_snprintf(gc->_priv->result_string,
                   sizeof(gc->_priv->result_string), "%s", buf);

        gtk_widget_queue_draw (gc->_priv->display);

        gtk_signal_emit(GTK_OBJECT(gc),
                        gtk_calc_signals[RESULT_CHANGED_SIGNAL],
                        gc->_priv->result);

}

static void
unselect_invert(GtkCalc *gc)
{
        g_return_if_fail(gc != NULL);
        g_return_if_fail(GTK_IS_CALC(gc));

        gc->_priv->invert=FALSE;

        /* not every keypad has the button */
        if(gc->_priv->invert_button)
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gc->_priv->invert_button),
                                             FALSE);
}

static void
setup_drg_label(GtkCalc *gc)
{
        GtkWidget *label;

        g_return_if_fail(gc != NULL);
        g_return_if_fail(GTK_IS_CALC(gc));

        /* not every keypad has the button */
        if(!gc->_priv->drg_button)
                return;

        label = GTK_BUTTON(gc->_priv->drg_button)->child;

        if(gc->_priv->mode == GTK_CALC_DEG)
                gtk_label_set_text(GTK_LABEL(label), _("DEG"));
        else if(gc->_priv->mode == GTK_CALC_RAD)
                gtk_label_set_text(GTK_LABEL(label), _("RAD"));
        else
                gtk_label_set_text(GTK_LABEL(label), _("GRAD"));
}

static gdouble
convert_num(gdouble num, GtkCalcMode from, GtkCalcMode to)
{
        if(to==from)
                return num;
        else if(from==GTK_CALC_DEG)
                if(to==GTK_CALC_RAD)
                        return (num*M_PI)/180;
                else /*GRAD*/
                        return (num*200)/180;
        else if(from==GTK_CALC_RAD)
                if(to==GTK_CALC_DEG)
                        return (num*180)/M_PI;
                else /*GRAD*/
                        return (num*200)/M_PI;
        else /*GRAD*/
                if(to==GTK_CALC_DEG)
                        return (num*180)/200;
                else /*RAD*/
                        return (num*M_PI)/200;
}

static void
no_func(GtkWidget *w, gpointer data G_GNUC_UNUSED)
{
        GtkCalc *gc = gtk_object_get_user_data(GTK_OBJECT(w));

        g_return_if_fail(gc!=NULL);

        if(gc->_priv->error)
                return;

        push_input(gc);

        /* if no stack, nothing happens */
        if(!gc->_priv->stack) {
                unselect_invert(gc);
                return;
        }

        reduce_stack_prec(gc,NULL,NULL);
        if(gc->_priv->error) return;
        set_result(gc);

        unselect_invert(gc);
}

static void
simple_func(GtkWidget *w, gpointer data)
{
        GtkCalc *gc = gtk_object_get_user_data(GTK_OBJECT(w));
        CalculatorStack *stack;
        CalculatorButton *but = data;
        MathFunction1 func = but->func1;
        MathFunction1 invfunc = but->invfunc1;

        g_return_if_fail(func!=NULL);
        g_return_if_fail(gc!=NULL);

        if(gc->_priv->error)
                return;

        push_input(gc);

        if(!gc->_priv->stack) {
                unselect_invert(gc);
                return;
        }

        stack = gc->_priv->stack->data;
        if(stack->type!=CALCULATOR_NUMBER) {
                unselect_invert(gc);
                return;
        }

        /*only convert non inverting functions*/
        if(!gc->_priv->invert && but->convert_to_rad)
                stack->d.number = convert_num(stack->d.number,
                                              gc->_priv->mode,
                                              GTK_CALC_RAD);

        errno = 0;
        {
                void (*old)(int) = signal(SIGFPE,sigfpe_handler);
                if(!gc->_priv->invert || invfunc==NULL)
                        stack->d.number = (*func)(stack->d.number);
                else
                        stack->d.number = (*invfunc)(stack->d.number);
                signal(SIGFPE,old);
        }

        if(errno>0 ||
           !isfinite(stack->d.number)) {
                errno = 0;
                do_error(gc);
                return;
        }

        /*we are converting back from rad to mode*/
        if(gc->_priv->invert && but->convert_to_rad)
                stack->d.number = convert_num(stack->d.number,
                                              GTK_CALC_RAD,
                                              gc->_priv->mode);

        set_result(gc);

        unselect_invert(gc);
}

/* a function of one word, on the programmer's keypad */
static void
int_func(GtkWidget *w, gpointer data)
{
        GtkCalc *gc = gtk_object_get_user_data(GTK_OBJECT(w));
        CalculatorStack *stack;
        CalculatorButton *but = data;

        g_return_if_fail(gc!=NULL);
        g_return_if_fail(but->ifunc1!=NULL);

        if(gc->_priv->error)
                return;

        push_input(gc);

        if(!gc->_priv->stack)
                return;

        stack = gc->_priv->stack->data;
        if(stack->type!=CALCULATOR_NUMBER)
                return;

        stack->d.inum = (*but->ifunc1)(gc, stack->d.inum) & word_mask(gc);

        set_result(gc);
}

static void
math_func(GtkWidget *w, gpointer data)
{
        GtkCalc *gc = gtk_object_get_user_data(GTK_OBJECT(w));
        CalculatorStack *stack;
        CalculatorButton *but = data;
        MathFunction2 func = but->func2;
        IntFunction2 ifunc = but->ifunc2;

        g_return_if_fail(func!=NULL || ifunc!=NULL);
        g_return_if_fail(gc!=NULL);

        if(gc->_priv->error)
                return;

        push_input(gc);

        if(!gc->_priv->stack) {
                unselect_invert(gc);
                return;
        }

        reduce_stack_prec(gc,func,ifunc);
        if(gc->_priv->error) return;
        set_result(gc);

        stack = gc->_priv->stack->data;
        if(stack->type!=CALCULATOR_NUMBER) {
                unselect_invert(gc);
                return;
        }

        stack = g_new0(CalculatorStack,1);
        stack->type = CALCULATOR_FUNCTION;
        stack->func = func;
        stack->ifunc = ifunc;

        gc->_priv->stack = g_list_prepend(gc->_priv->stack,stack);

        unselect_invert(gc);
}

static void
reset_calc(GtkWidget *w, gpointer data)
{
        GtkCalc *gc;
        if(w)
                gc = gtk_object_get_user_data(GTK_OBJECT(w));
        else
                gc = data;

        g_return_if_fail(gc!=NULL);

        while(gc->_priv->stack)
                stack_pop(&gc->_priv->stack);

        gc->_priv->result = 0;
        gc->_priv->iresult = 0;
        gc->_priv->entry = 0;
        gc->_priv->entry_neg = FALSE;
        strcpy(gc->_priv->result_string, " 0");
        gc->_priv->memory = 0;
        gc->_priv->imemory = 0;
        gc->_priv->mode = GTK_CALC_DEG;
        gc->_priv->invert = FALSE;
        gc->_priv->error = FALSE;

        gc->_priv->add_digit = TRUE;
        push_input(gc);
        set_result(gc);

        unselect_invert(gc);
        setup_drg_label(gc);
}

static void
clear_calc(GtkWidget *w, gpointer data G_GNUC_UNUSED)
{
        GtkCalc *gc = gtk_object_get_user_data(GTK_OBJECT(w));

        g_return_if_fail(gc!=NULL);

        /* if in add digit mode, just clear the number, otherwise clear
         * state as well */
        if(!gc->_priv->add_digit) {
                while(gc->_priv->stack)
                        stack_pop(&gc->_priv->stack);
        }

        gc->_priv->result = 0;
        gc->_priv->iresult = 0;
        gc->_priv->entry = 0;
        gc->_priv->entry_neg = FALSE;
        strcpy(gc->_priv->result_string, " 0");
        gc->_priv->error = FALSE;
        gc->_priv->invert = FALSE;

        gc->_priv->add_digit = TRUE;
        push_input(gc);
        set_result(gc);

        unselect_invert(gc);
}


/**
 * gtk_calc_clear
 * @gc: Pointer to GNOME calculator widget.
 * @reset: %FALSE to zero, %TRUE to reset calculator completely
 *
 * Description:
 * Resets the calculator back to zero. If @reset is %TRUE, results
 * stored in memory and the calculator mode are cleared also.
 **/

void
gtk_calc_clear(GtkCalc *gc, const gboolean reset)
{
        if (reset)
                reset_calc(NULL, gc);
        else
                clear_calc(NULL, gc);
}

/* the largest magnitude that may be typed in the word */
static guint64
entry_limit(GtkCalc *gc, gboolean neg)
{
        guint64 mask = word_mask(gc);

        if(gc->_priv->base == GTK_CALC_BASE_DEC && gc->_priv->is_signed)
                return (mask >> 1) + (neg ? 1 : 0);
        return mask;
}

/* show what is being typed on the programmer's keypad */
static void
update_entry(GtkCalc *gc)
{
        guint64 v = gc->_priv->entry;

        if(gc->_priv->entry_neg)
                v = (guint64)0 - v;
        gc->_priv->iresult = v & word_mask(gc);

        if(gc->_priv->entry_neg && gc->_priv->entry == 0)
                strcpy(gc->_priv->result_string, "-0");
        else
                format_word(gc, gc->_priv->iresult,
                            gc->_priv->result_string,
                            sizeof(gc->_priv->result_string));
        redisplay(gc);
}

static void
add_int_digit(GtkCalc *gc, gchar c)
{
        guint radix = base_radix(gc->_priv->base);
        guint64 limit;
        guint d;

        if(c >= '0' && c <= '9')
                d = c - '0';
        else if(c >= 'A' && c <= 'F')
                d = c - 'A' + 10;
        else
                return;         /* the keypad's point, from the keyboard */
        if(d >= radix)
                return;

        if(!gc->_priv->add_digit) {
                if(gc->_priv->stack) {
                        CalculatorStack *stack=gc->_priv->stack->data;
                        if(stack->type==CALCULATOR_NUMBER)
                                stack_pop(&gc->_priv->stack);
                }
                gc->_priv->add_digit = TRUE;
                gc->_priv->entry = 0;
                gc->_priv->entry_neg = FALSE;
        }

        /* a digit that would not fit in the word is not taken */
        limit = entry_limit(gc, gc->_priv->entry_neg);
        if(gc->_priv->entry > (limit - d) / radix)
                return;
        gc->_priv->entry = gc->_priv->entry * radix + d;

        update_entry(gc);
}

static void
add_digit (GtkWidget *w, gpointer data)
{
        GtkCalc *gc = gtk_object_get_user_data (GTK_OBJECT (w));
        CalculatorButton *but = data;
        const gchar *digit = but->name;

        if(gc->_priv->error)
                return;

        /* For "EE" where name is not what we want to add */
        if (but->digit != NULL)
                digit = but->digit;

        g_return_if_fail(gc!=NULL);
        g_return_if_fail(digit!=NULL);

        if(gc->_priv->layout == GTK_CALC_PROGRAMMER) {
                add_int_digit(gc, digit[0]);
                return;
        }

        if(!gc->_priv->add_digit) {
                if(gc->_priv->stack) {
                        CalculatorStack *stack=gc->_priv->stack->data;
                        if(stack->type==CALCULATOR_NUMBER)
                                stack_pop(&gc->_priv->stack);
                }
                gc->_priv->add_digit = TRUE;
                gc->_priv->result_string[0] = '\0';
        }

        unselect_invert(gc);

        if(digit[0]=='e') {
                if(strchr(gc->_priv->result_string,'e'))
                        return;
                else if(strlen(gc->_priv->result_string)>9)
                        return;
                else if(gc->_priv->result_string[0]=='\0')
                        strcpy(gc->_priv->result_string," 1");
        } else if(digit[0]=='.') {
                if(strchr(gc->_priv->result_string,'.'))
                        return;
                else if(strlen(gc->_priv->result_string)>10)
                        return;
                else if(gc->_priv->result_string[0]=='\0')
                        strcpy(gc->_priv->result_string," 0");
        } else { /*numeric*/
                if(strlen(gc->_priv->result_string)>11)
                        return;
                else if (strcmp (gc->_priv->result_string, " 0") == 0 ||
                         gc->_priv->result_string[0]=='\0')
                        strcpy(gc->_priv->result_string," ");
        }

        strcat(gc->_priv->result_string,digit);

        gtk_widget_queue_draw (gc->_priv->display);

        {
                gchar *old_locale;
                /* make sure get values in a consistent manner */
                old_locale = g_strdup (setlocale (LC_NUMERIC, NULL));
                setlocale (LC_NUMERIC, "C");
                sscanf(gc->_priv->result_string, "%lf", &gc->_priv->result);
                setlocale (LC_NUMERIC, old_locale);
                g_free (old_locale);
        }

}

static gdouble
c_neg(gdouble arg)
{
        return -arg;
}


static void
negate_val(GtkWidget *w, gpointer data)
{
        GtkCalc *gc = gtk_object_get_user_data(GTK_OBJECT(w));
        char *p;

        g_return_if_fail(gc!=NULL);

        if(gc->_priv->error)
                return;

        unselect_invert(gc);

        if(gc->_priv->layout == GTK_CALC_PROGRAMMER) {
                /* Only a signed decimal is typed with a sign; otherwise
                 * the sign changes the word, two's complement. */
                if(gc->_priv->add_digit &&
                   gc->_priv->base == GTK_CALC_BASE_DEC &&
                   gc->_priv->is_signed) {
                        if(gc->_priv->entry >
                           entry_limit(gc, !gc->_priv->entry_neg))
                                return;
                        gc->_priv->entry_neg = !gc->_priv->entry_neg;
                        update_entry(gc);
                } else
                        int_func(w,data);
                return;
        }

        if(!gc->_priv->add_digit) {
                simple_func(w,data);
                return;
        }

        if((p=strchr(gc->_priv->result_string,'e'))!=NULL) {
                p++;
                if(*p=='-')
                        *p='+';
                else
                        *p='-';
        } else {
                if(gc->_priv->result_string[0]=='-')
                        gc->_priv->result_string[0]=' ';
                else
                        gc->_priv->result_string[0]='-';
        }

        /* make sure get values in a consistent manner */
        {
                gchar *old_locale;
                /* make sure get values in a consistent manner */
                old_locale = g_strdup (setlocale (LC_NUMERIC, NULL));
                setlocale (LC_NUMERIC, "C");
                sscanf(gc->_priv->result_string, "%lf", &gc->_priv->result);
                setlocale (LC_NUMERIC, old_locale);
                g_free (old_locale);
        }

        gtk_widget_queue_draw (gc->_priv->display);
}

static gdouble
c_inv(gdouble arg1)
{
        if(arg1==0) {
                errno=ERANGE;
                return 0;
        }
        return 1/arg1;
}

static gdouble
c_pow2(gdouble arg1)
{
        return pow(arg1,2);
}

static gdouble
c_pow10(gdouble arg1)
{
        return pow(10,arg1);
}

static gdouble
c_powe(gdouble arg1)
{
        return pow(M_E,arg1);
}

static gdouble
c_fact(gdouble arg1)
{
        int i;
        gdouble r;
        if(arg1<0) {
                errno=ERANGE;
                return 0;
        }
        i = (int)(arg1+0.5);
        if((fabs(arg1-i))>1e-9) {
                errno=ERANGE;
                return 0;
        }
        for(r=1;i>0;i--)
                r*=i;

        return r;
}

static gdouble
set_result_to(GtkCalc *gc, gdouble result)
{
        gdouble old;

        if(gc->_priv->stack==NULL ||
           ((CalculatorStack *)gc->_priv->stack->data)->type!=CALCULATOR_NUMBER) {
                gc->_priv->add_digit = TRUE;
                old = gc->_priv->result;
                gc->_priv->result = result;
                push_input(gc);
        } else {
                old = ((CalculatorStack *)gc->_priv->stack->data)->d.number;
                ((CalculatorStack *)gc->_priv->stack->data)->d.number = result;
        }

        set_result(gc);

        return old;
}


static guint64
set_word_to(GtkCalc *gc, guint64 result)
{
        guint64 old;

        result &= word_mask(gc);

        if(gc->_priv->stack==NULL ||
           ((CalculatorStack *)gc->_priv->stack->data)->type!=CALCULATOR_NUMBER) {
                gc->_priv->add_digit = TRUE;
                old = gc->_priv->iresult;
                gc->_priv->iresult = result;
                push_input(gc);
        } else {
                old = ((CalculatorStack *)gc->_priv->stack->data)->d.inum;
                ((CalculatorStack *)gc->_priv->stack->data)->d.inum = result;
        }

        set_result(gc);

        return old;
}

/**
 * gtk_calc_set
 * @gc: Pointer to GNOME calculator widget.
 * @result: New value of calculator buffer.
 *
 * Description:  Sets the value stored in the calculator's result buffer to the
 * given @result.
 **/

void
gtk_calc_set(GtkCalc *gc, gdouble result)
{
        if(gc->_priv->layout == GTK_CALC_PROGRAMMER)
                set_word_to(gc, double_to_word(gc, result));
        else
                set_result_to(gc,result);
}

static void
store_m(GtkWidget *w, gpointer data G_GNUC_UNUSED)
{
        GtkCalc *gc = gtk_object_get_user_data(GTK_OBJECT(w));

        g_return_if_fail(gc!=NULL);

        if(gc->_priv->error)
                return;

        push_input(gc);

        if(gc->_priv->layout == GTK_CALC_PROGRAMMER)
                gc->_priv->imemory = gc->_priv->iresult;
        else
                gc->_priv->memory = gc->_priv->result;

        gtk_widget_queue_draw (gc->_priv->display);

        unselect_invert(gc);
}

static void
recall_m(GtkWidget *w, gpointer data G_GNUC_UNUSED)
{
        GtkCalc *gc = gtk_object_get_user_data(GTK_OBJECT(w));

        g_return_if_fail(gc!=NULL);

        if(gc->_priv->error)
                return;

        if(gc->_priv->layout == GTK_CALC_PROGRAMMER)
                set_word_to(gc,gc->_priv->imemory);
        else
                set_result_to(gc,gc->_priv->memory);

        unselect_invert(gc);
}

static void
sum_m(GtkWidget *w, gpointer data G_GNUC_UNUSED)
{
        GtkCalc *gc = gtk_object_get_user_data(GTK_OBJECT(w));

        g_return_if_fail(gc!=NULL);

        if(gc->_priv->error)
                return;

        push_input(gc);

        if(gc->_priv->layout == GTK_CALC_PROGRAMMER)
                gc->_priv->imemory = (gc->_priv->imemory + gc->_priv->iresult) &
                        word_mask(gc);
        else
                gc->_priv->memory += gc->_priv->result;

        gtk_widget_queue_draw (gc->_priv->display);

        unselect_invert(gc);
}

static void
exchange_m(GtkWidget *w, gpointer data G_GNUC_UNUSED)
{
        GtkCalc *gc = gtk_object_get_user_data(GTK_OBJECT(w));

        g_return_if_fail(gc!=NULL);

        if(gc->_priv->error)
                return;

        if(gc->_priv->layout == GTK_CALC_PROGRAMMER)
                gc->_priv->imemory = set_word_to(gc,gc->_priv->imemory);
        else
                gc->_priv->memory = set_result_to(gc,gc->_priv->memory);

        unselect_invert(gc);
}

static void
invert_toggle(GtkWidget *w, gpointer data G_GNUC_UNUSED)
{
        GtkCalc *gc = gtk_object_get_user_data(GTK_OBJECT(w));

        g_return_if_fail(gc!=NULL);

        if(gc->_priv->error)
                return;

        if(GTK_TOGGLE_BUTTON(w)->active)
                gc->_priv->invert=TRUE;
        else
                gc->_priv->invert=FALSE;
}

static void
drg_toggle(GtkWidget *w, gpointer data G_GNUC_UNUSED)
{
        GtkCalc *gc = gtk_object_get_user_data(GTK_OBJECT(w));
        GtkCalcMode oldmode;

        g_return_if_fail(gc!=NULL);

        if(gc->_priv->error)
                return;

        oldmode = gc->_priv->mode;

        if(gc->_priv->mode==GTK_CALC_DEG)
                gc->_priv->mode=GTK_CALC_RAD;
        else if(gc->_priv->mode==GTK_CALC_RAD)
                gc->_priv->mode=GTK_CALC_GRAD;
        else
                gc->_priv->mode=GTK_CALC_DEG;

        setup_drg_label(gc);

        /*convert if invert is on*/
        if(gc->_priv->invert) {
                CalculatorStack *stack;
                push_input(gc);
                stack = gc->_priv->stack->data;
                stack->d.number = convert_num(stack->d.number,
                                              oldmode,gc->_priv->mode);
                set_result(gc);
        }

        unselect_invert(gc);
}

static void
set_pi(GtkWidget *w, gpointer data G_GNUC_UNUSED)
{
        GtkCalc *gc = gtk_object_get_user_data(GTK_OBJECT(w));

        g_return_if_fail(gc!=NULL);

        if(gc->_priv->error)
                return;

        set_result_to(gc,M_PI);

        unselect_invert(gc);
}

static void
set_e(GtkWidget *w, gpointer data G_GNUC_UNUSED)
{
        GtkCalc *gc = gtk_object_get_user_data(GTK_OBJECT(w));

        g_return_if_fail(gc!=NULL);

        if(gc->_priv->error)
                return;

        set_result_to(gc,M_E);

        unselect_invert(gc);
}

static void
add_parenth(GtkWidget *w, gpointer data G_GNUC_UNUSED)
{
        GtkCalc *gc = gtk_object_get_user_data(GTK_OBJECT(w));

        g_return_if_fail(gc!=NULL);

        if(gc->_priv->error)
                return;

        if(gc->_priv->stack &&
           ((CalculatorStack *)gc->_priv->stack->data)->type==CALCULATOR_NUMBER)
                ((CalculatorStack *)gc->_priv->stack->data)->type =
                        CALCULATOR_PARENTHESIS;
        else {
                CalculatorStack *stack;
                stack = g_new(CalculatorStack,1);
                stack->type = CALCULATOR_PARENTHESIS;
                gc->_priv->stack = g_list_prepend(gc->_priv->stack,stack);
        }

        unselect_invert(gc);
}

static void
sub_parenth(GtkWidget *w, gpointer data G_GNUC_UNUSED)
{
        GtkCalc *gc = gtk_object_get_user_data(GTK_OBJECT(w));
        g_return_if_fail(gc!=NULL);

        if(gc->_priv->error)
                return;

        push_input(gc);
        reduce_stack_prec(gc,NULL,NULL);
        if(gc->_priv->error) return;
        set_result(gc);

        if(gc->_priv->stack) {
                CalculatorStack *stack = gc->_priv->stack->data;
                if(stack->type==CALCULATOR_PARENTHESIS)
                        stack_pop(&gc->_priv->stack);
                else if(g_list_next(gc->_priv->stack)) {
                        stack = g_list_next(gc->_priv->stack)->data;
                        if(stack->type==CALCULATOR_PARENTHESIS) {
                                GList *n = g_list_next(gc->_priv->stack);
                                gc->_priv->stack = g_list_remove_link(gc->_priv->stack,n);
                                g_list_free_1(n);
                        }
                }
        }

        unselect_invert(gc);
}

/*
 * The programmer's base, word size and signedness. Changing the word
 * size or signedness ends the number being typed, and a word size
 * shrinking cuts every word down to it.
 */
static void
set_button_label(GtkWidget *button, const gchar *text)
{
        if(button)
                gtk_label_set_text(GTK_LABEL(GTK_BUTTON(button)->child), text);
}

static void
setup_base_keys(GtkCalc *gc)
{
        static const gchar *names[] = {
                N_("HEX"), N_("DEC"), N_("OCT"), N_("BIN")
        };
        guint radix = base_radix(gc->_priv->base);
        guint i;

        set_button_label(gc->_priv->base_button, _(names[gc->_priv->base]));

        for(i = 0; i < 16; i++) {
                if(gc->_priv->digit_buttons[i])
                        gtk_widget_set_sensitive(gc->_priv->digit_buttons[i],
                                                 i < radix);
        }
}

static void
setup_width_label(GtkCalc *gc)
{
        gchar buf[32];

        g_snprintf(buf, sizeof(buf), _("%u-bit"), gc->_priv->width);
        set_button_label(gc->_priv->width_button, buf);
}

/* show the word again, after its base, size or signedness changed */
static void
refresh_word(GtkCalc *gc)
{
        if(!gc->_priv->error) {
                push_input(gc);
                gc->_priv->iresult &= word_mask(gc);
                format_word(gc, gc->_priv->iresult,
                            gc->_priv->result_string,
                            sizeof(gc->_priv->result_string));
        }
        redisplay(gc);
}

static void
set_base(GtkCalc *gc, GtkCalcBase base)
{
        gc->_priv->base = base;
        setup_base_keys(gc);

        /* What is being typed carries on in the new base, unless it was
         * typed with a sign or no longer fits as it was typed. */
        if(gc->_priv->add_digit && !gc->_priv->error &&
           !gc->_priv->entry_neg &&
           gc->_priv->entry <= entry_limit(gc, FALSE)) {
                update_entry(gc);
                return;
        }
        refresh_word(gc);
}

static void
set_width(GtkCalc *gc, guint width)
{
        GList *list;

        gc->_priv->width = width;
        setup_width_label(gc);

        if(!gc->_priv->error)
                push_input(gc);
        for(list = gc->_priv->stack; list; list = g_list_next(list)) {
                CalculatorStack *stack = list->data;
                if(stack->type == CALCULATOR_NUMBER)
                        stack->d.inum &= word_mask(gc);
        }
        gc->_priv->imemory &= word_mask(gc);

        refresh_word(gc);
}

static void
base_cycle(GtkWidget *w, gpointer data G_GNUC_UNUSED)
{
        GtkCalc *gc = gtk_object_get_user_data(GTK_OBJECT(w));

        g_return_if_fail(gc!=NULL);

        set_base(gc, (gc->_priv->base + 1) % 4);
}

static void
width_cycle(GtkWidget *w, gpointer data G_GNUC_UNUSED)
{
        GtkCalc *gc = gtk_object_get_user_data(GTK_OBJECT(w));

        g_return_if_fail(gc!=NULL);

        set_width(gc, gc->_priv->width >= 64 ? 8 : gc->_priv->width * 2);
}

static void
signed_toggle(GtkWidget *w, gpointer data G_GNUC_UNUSED)
{
        GtkCalc *gc = gtk_object_get_user_data(GTK_OBJECT(w));

        g_return_if_fail(gc!=NULL);

        gc->_priv->is_signed = GTK_TOGGLE_BUTTON(w)->active != FALSE;
        refresh_word(gc);
}

static const CalculatorButton scientific_buttons[8][5] = {
        {
                { .name = N_("1/x"),  .signal_func = simple_func, .func1 = c_inv },
                { .name = N_("x^2"),  .signal_func = simple_func, .func1 = c_pow2, .invfunc1 = sqrt },
                { .name = N_("SQRT"), .signal_func = simple_func, .func1 = sqrt, .invfunc1 = c_pow2,
                  .keys = {'r','R',0} },
                { .name = N_("CE/C"), .signal_func = clear_calc,
                  .keys = {GDK_Clear,GDK_Delete,0} },
                { .name = N_("AC"),   .signal_func = reset_calc,
                  .keys = {'a','A', GDK_Escape, 0} }
        },{
                { .name = N_("INV"),  .signal_func = invert_toggle, .toggle = TRUE,
                  .keys = {'i','I',0} },
                { .name = N_("sin"),  .signal_func = simple_func, .func1 = sin, .invfunc1 = asin,
                  .convert_to_rad = TRUE, .keys = {'s','S',0} },
                { .name = N_("cos"),  .signal_func = simple_func, .func1 = cos, .invfunc1 = acos,
                  .convert_to_rad = TRUE, .keys = {'c','C',0} },
                { .name = N_("tan"),  .signal_func = simple_func, .func1 = tan, .invfunc1 = atan,
                  .convert_to_rad = TRUE, .keys = {'t','T',0} },
                { .name = N_("DEG"),  .signal_func = drg_toggle,
                  .keys = {'d','D',0} }
        },{
                { .name = N_("e"),    .signal_func = set_e,
                  .keys = {'e','E',0} },
                { .name = N_("EE"),   .signal_func = add_digit, .digit = "e+" },
                { .name = N_("log"),  .signal_func = simple_func, .func1 = log10, .invfunc1 = c_pow10 },
                { .name = N_("ln"),   .signal_func = simple_func, .func1 = log, .invfunc1 = c_powe,
                  .keys = {'l','L',0} },
                { .name = N_("x^y"),  .signal_func = math_func, .func2 = pow,
                  .keys = {'^',0} }
        },{
                { .name = N_("PI"),   .signal_func = set_pi,
                  .keys = {'p','P',0} },
                { .name = N_("x!"),   .signal_func = simple_func, .func1 = c_fact,
                  .keys = {'!',0} },
                { .name = N_("("),    .signal_func = add_parenth,
                  .keys = {'(',0} },
                { .name = N_(")"),    .signal_func = sub_parenth,
                  .keys = {')',0} },
                { .name = N_("/"),    .signal_func = math_func, .func2 = c_div,
                  .keys = {'/',GDK_KP_Divide,0} }
        },{
                { .name = N_("STO"),  .signal_func = store_m },
                { .name = N_("7"),    .signal_func = add_digit,
                  .keys = {'7',GDK_KP_7,GDK_KP_Home,0} },
                { .name = N_("8"),    .signal_func = add_digit,
                  .keys = {'8',GDK_KP_8,GDK_KP_Up,0} },
                { .name = N_("9"),    .signal_func = add_digit,
                  .keys = {'9',GDK_KP_9,GDK_KP_Page_Up,0} },
                { .name = N_("*"),    .signal_func = math_func, .func2 = c_mul,
                  .keys = {'*',GDK_KP_Multiply,0} }
        },{
                { .name = N_("RCL"),  .signal_func = recall_m },
                { .name = N_("4"),    .signal_func = add_digit,
                  .keys = {'4',GDK_KP_4,GDK_KP_Left,0} },
                { .name = N_("5"),    .signal_func = add_digit,
                  .keys = {'5',GDK_KP_5,GDK_KP_Begin,0} },
                { .name = N_("6"),    .signal_func = add_digit,
                  .keys = {'6',GDK_KP_6,GDK_KP_Right,0} },
                { .name = N_("-"),    .signal_func = math_func, .func2 = c_sub,
                  .keys = {'-',GDK_KP_Subtract,0} }
        },{
                { .name = N_("SUM"),  .signal_func = sum_m },
                { .name = N_("1"),    .signal_func = add_digit,
                  .keys = {'1',GDK_KP_1,GDK_KP_End,0} },
                { .name = N_("2"),    .signal_func = add_digit,
                  .keys = {'2',GDK_KP_2,GDK_KP_Down,0} },
                { .name = N_("3"),    .signal_func = add_digit,
                  .keys = {'3',GDK_KP_3,GDK_KP_Page_Down,0} },
                { .name = N_("+"),    .signal_func = math_func, .func2 = c_add,
                  .keys = {'+',GDK_KP_Add,0} }
        },{
                { .name = N_("EXC"),  .signal_func = exchange_m },
                { .name = N_("0"),    .signal_func = add_digit,
                  .keys = {'0',GDK_KP_0,GDK_KP_Insert,0} },
                { .name = N_("."),    .signal_func = add_digit,
                  .keys = {'.',GDK_KP_Decimal,',',GDK_KP_Delete,0} },
                { .name = N_("+/-"),  .signal_func = negate_val, .func1 = c_neg },
                { .name = N_("="),    .signal_func = no_func,
                  .keys = {'=',GDK_KP_Enter,0} }
        }
};

/* The digits are laid out as on the KIM-1's hex keypad. */
#define HEX_KEY(hex, lc, uc) \
        { .name = hex, .signal_func = add_digit, .keys = {lc, uc, 0} }

static const CalculatorButton programmer_buttons[8][5] = {
        {
                { .name = N_("HEX"),  .signal_func = base_cycle },
                { .name = N_("32-bit"), .signal_func = width_cycle },
                { .name = N_("SGN"),  .signal_func = signed_toggle, .toggle = TRUE },
                { .name = N_("CE/C"), .signal_func = clear_calc,
                  .keys = {GDK_Clear,GDK_Delete,0} },
                { .name = N_("AC"),   .signal_func = reset_calc,
                  .keys = {GDK_Escape,0} }
        },{
                { .name = N_("AND"),  .signal_func = math_func, .ifunc2 = i_and,
                  .keys = {GDK_ampersand,0} },
                { .name = N_("OR"),   .signal_func = math_func, .ifunc2 = i_or,
                  .keys = {GDK_bar,0} },
                { .name = N_("XOR"),  .signal_func = math_func, .ifunc2 = i_xor,
                  .keys = {GDK_asciicircum,0} },
                { .name = N_("NOT"),  .signal_func = int_func, .ifunc1 = i_not,
                  .keys = {GDK_asciitilde,0} },
                { .name = N_("MOD"),  .signal_func = math_func, .ifunc2 = i_mod,
                  .keys = {GDK_percent,0} }
        },{
                { .name = N_("<<"),   .signal_func = math_func, .ifunc2 = i_shl,
                  .keys = {GDK_less,0} },
                { .name = N_(">>"),   .signal_func = math_func, .ifunc2 = i_shr,
                  .keys = {GDK_greater,0} },
                { .name = N_("("),    .signal_func = add_parenth,
                  .keys = {'(',0} },
                { .name = N_(")"),    .signal_func = sub_parenth,
                  .keys = {')',0} },
                { .name = N_("+/-"),  .signal_func = negate_val, .ifunc1 = i_neg }
        },{
                { .name = N_("STO"),  .signal_func = store_m },
                { .name = N_("RCL"),  .signal_func = recall_m },
                { .name = N_("SUM"),  .signal_func = sum_m },
                { .name = N_("EXC"),  .signal_func = exchange_m },
                { .name = N_("/"),    .signal_func = math_func, .ifunc2 = i_div,
                  .keys = {'/',GDK_KP_Divide,0} }
        },{
                HEX_KEY("C", 'c', 'C'), HEX_KEY("D", 'd', 'D'),
                HEX_KEY("E", 'e', 'E'), HEX_KEY("F", 'f', 'F'),
                { .name = N_("*"),    .signal_func = math_func, .ifunc2 = i_mul,
                  .keys = {'*',GDK_KP_Multiply,0} }
        },{
                { .name = "8",        .signal_func = add_digit,
                  .keys = {'8',GDK_KP_8,GDK_KP_Up,0} },
                { .name = "9",        .signal_func = add_digit,
                  .keys = {'9',GDK_KP_9,GDK_KP_Page_Up,0} },
                HEX_KEY("A", 'a', 'A'), HEX_KEY("B", 'b', 'B'),
                { .name = N_("-"),    .signal_func = math_func, .ifunc2 = i_sub,
                  .keys = {'-',GDK_KP_Subtract,0} }
        },{
                { .name = "4",        .signal_func = add_digit,
                  .keys = {'4',GDK_KP_4,GDK_KP_Left,0} },
                { .name = "5",        .signal_func = add_digit,
                  .keys = {'5',GDK_KP_5,GDK_KP_Begin,0} },
                { .name = "6",        .signal_func = add_digit,
                  .keys = {'6',GDK_KP_6,GDK_KP_Right,0} },
                { .name = "7",        .signal_func = add_digit,
                  .keys = {'7',GDK_KP_7,GDK_KP_Home,0} },
                { .name = N_("+"),    .signal_func = math_func, .ifunc2 = i_add,
                  .keys = {'+',GDK_KP_Add,0} }
        },{
                { .name = "0",        .signal_func = add_digit,
                  .keys = {'0',GDK_KP_0,GDK_KP_Insert,0} },
                { .name = "1",        .signal_func = add_digit,
                  .keys = {'1',GDK_KP_1,GDK_KP_End,0} },
                { .name = "2",        .signal_func = add_digit,
                  .keys = {'2',GDK_KP_2,GDK_KP_Down,0} },
                { .name = "3",        .signal_func = add_digit,
                  .keys = {'3',GDK_KP_3,GDK_KP_Page_Down,0} },
                { .name = N_("="),    .signal_func = no_func,
                  .keys = {'=',GDK_KP_Enter,0} }
        }
};

#undef HEX_KEY

/*
 * The display's characters, drawn as the segments of a pocket
 * calculator's. A segment is three pixels thick and pointed at its
 * ends; A to G are the usual seven, clockwise from the top, with G
 * across the middle.
 */
enum {
        SEG_A = 1 << 0,
        SEG_B = 1 << 1,
        SEG_C = 1 << 2,
        SEG_D = 1 << 3,
        SEG_E = 1 << 4,
        SEG_F = 1 << 5,
        SEG_G = 1 << 6
};

static const guint digit_segments[10] = {
        SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,          /* 0 */
        SEG_B | SEG_C,                                          /* 1 */
        SEG_A | SEG_B | SEG_D | SEG_E | SEG_G,                  /* 2 */
        SEG_A | SEG_B | SEG_C | SEG_D | SEG_G,                  /* 3 */
        SEG_B | SEG_C | SEG_F | SEG_G,                          /* 4 */
        SEG_A | SEG_C | SEG_D | SEG_F | SEG_G,                  /* 5 */
        SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,          /* 6 */
        SEG_A | SEG_B | SEG_C,                                  /* 7 */
        SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,  /* 8 */
        SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G           /* 9 */
};

/* the hex digits as a pocket calculator's: A b C d E F */
static const guint hex_segments[6] = {
        SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G,          /* A */
        SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,                  /* b */
        SEG_A | SEG_D | SEG_E | SEG_F,                          /* C */
        SEG_B | SEG_C | SEG_D | SEG_E | SEG_G,                  /* d */
        SEG_A | SEG_D | SEG_E | SEG_F | SEG_G,                  /* E */
        SEG_A | SEG_E | SEG_F | SEG_G                           /* F */
};

/* a segment lying down, its top row at y */
static void
draw_across (GdkDrawable *d, GdkGC *gc, int x, int y)
{
        gdk_draw_rectangle (d, gc, TRUE, x + 6, y,     8,  1);
        gdk_draw_rectangle (d, gc, TRUE, x + 5, y + 1, 10, 1);
        gdk_draw_rectangle (d, gc, TRUE, x + 6, y + 2, 8,  1);
}

/* a segment standing up, its left column at x and its top at y */
static void
draw_upright (GdkDrawable *d, GdkGC *gc, int x, int y)
{
        gdk_draw_rectangle (d, gc, TRUE, x,     y + 1, 1, 8);
        gdk_draw_rectangle (d, gc, TRUE, x + 1, y,     1, 10);
        gdk_draw_rectangle (d, gc, TRUE, x + 2, y + 1, 1, 8);
}

static void
draw_segments (GdkDrawable *d, GdkGC *gc, int x, guint segs)
{
        if (segs & SEG_A) draw_across  (d, gc, x,      2);
        if (segs & SEG_B) draw_upright (d, gc, x + 14, 4);
        if (segs & SEG_C) draw_upright (d, gc, x + 14, 15);
        if (segs & SEG_D) draw_across  (d, gc, x,      24);
        if (segs & SEG_E) draw_upright (d, gc, x + 3,  15);
        if (segs & SEG_F) draw_upright (d, gc, x + 3,  4);
        if (segs & SEG_G) draw_across  (d, gc, x,      13);
}

/* one character of the display in the cell at x */
static void
draw_char (GdkDrawable *d, GdkGC *gc, int x, char c)
{
        if (c >= '0' && c <= '9')
                draw_segments (d, gc, x, digit_segments[c - '0']);
        else if (c >= 'A' && c <= 'F')
                draw_segments (d, gc, x, hex_segments[c - 'A']);
        else if (c == '.')
                gdk_draw_rectangle (d, gc, TRUE, x + 8, 24, 3, 3);
        else if (c == '+') {
                draw_segments (d, gc, x, SEG_G);
                gdk_draw_rectangle (d, gc, TRUE, x + 8,  10, 1, 9);
                gdk_draw_rectangle (d, gc, TRUE, x + 9,  9,  1, 11);
                gdk_draw_rectangle (d, gc, TRUE, x + 10, 10, 1, 9);
        }
        else if (c == '-')
                draw_segments (d, gc, x, SEG_G);
        else if (c == 'e')
                draw_segments (d, gc, x, SEG_A | SEG_D | SEG_E | SEG_F | SEG_G);
}

/* the M that says something is in memory, small, at the top of a cell */
static void
draw_memory_mark (GdkDrawable *d, GdkGC *gc, int x)
{
        int i;

        for (i = 0; i < 2; i++) {
                gdk_draw_line (d, gc, x + 3 + i,  2, x + 3 + i,  10);
                gdk_draw_line (d, gc, x + 12 + i, 2, x + 12 + i, 10);
                gdk_draw_line (d, gc, x + 3 + i,  2, x + 7 + i,  6);
                gdk_draw_line (d, gc, x + 12 + i, 2, x + 8 + i,  6);
        }
}

/* the arrow that says there are more digits to the left than fit */
static void
draw_more_mark (GdkDrawable *d, GdkGC *gc, int x)
{
        GdkPoint arrow[3] = {
                { x + 14, 8 }, { x + 14, 20 }, { x + 6, 14 }
        };

        gdk_draw_polygon (d, gc, TRUE, arrow, 3);
}

static void
display_realize (GtkWidget *widget, gpointer data)
{
        GtkCalc *calc = GTK_CALC (data);
        GdkColor lit = { 0, 0x4040, 0xe8e8, 0x4040 };

        gdk_colormap_alloc_color (gtk_widget_get_colormap (widget),
                                  &lit, FALSE, TRUE);
        calc->_priv->lit_gc = gdk_gc_new (widget->window);
        gdk_gc_set_foreground (calc->_priv->lit_gc, &lit);
}

static void
display_unrealize (GtkWidget *widget G_GNUC_UNUSED, gpointer data)
{
        GtkCalc *calc = GTK_CALC (data);

        if (calc->_priv->lit_gc) {
                gdk_gc_unref (calc->_priv->lit_gc);
                calc->_priv->lit_gc = NULL;
        }
}

/* Expose handler for the calculator display drawing area */
static gint
display_expose (GtkWidget *widget G_GNUC_UNUSED, GdkEventExpose *event G_GNUC_UNUSED, gpointer data)
{
        GtkCalc *calc;
        GdkWindow *window;
        GdkGC *gc;
        const char *text;
        int x, i, len, first, cells;
        gboolean memory;

        int win_width, win_height;

        calc = GTK_CALC (data);

        window = calc->_priv->display->window;

        gdk_window_get_size (window, &win_width, &win_height);

        gdk_draw_rectangle (window, calc->_priv->display->style->black_gc,
                            TRUE, 0, 0, win_width, win_height);

        gc = calc->_priv->lit_gc;
        if (!gc)
                return TRUE;

        if (calc->_priv->layout == GTK_CALC_PROGRAMMER) {
                cells = PROG_DISPLAY_LEN;
                memory = calc->_priv->imemory != 0;
        } else {
                cells = DISPLAY_LEN;
                memory = calc->_priv->memory != 0;
        }

        /* the numbers keep to the right, the mark to the left of them */
        if (memory)
                draw_memory_mark (window, gc,
                                  win_width - cells * FONT_WIDTH);

        /* a blank is only where a sign would go */
        text = calc->_priv->result_string;
        if (text[0] == ' ')
                text++;
        len = strlen (text);

        /* Only a long binary word is too much for the display: show its
         * low digits, and an arrow for the rest; the bits are all there
         * below. */
        first = 0;
        if (len > cells - 1) {
                first = len - (cells - 2);
                draw_more_mark (window, gc,
                                win_width - (cells - 1) * FONT_WIDTH);
        }

        for (x = 1, i = len - 1; i >= first; x++, i--)
                draw_char (window, gc, win_width - x * FONT_WIDTH, text[i]);

        return TRUE;
}

/*
 * The bit strip: the programmer's word in binary, all 64 bits in two
 * rows of nibbles with bit numbers beneath, those above the word size
 * greyed. Clicking a bit flips it.
 */
#define BITS_PAD 4

typedef struct {
        gint cell;              /* the width of one bit */
        gint line;              /* the height of a line of text */
        gint row;               /* the height of a row of bits and numbers */
        gint x;                 /* the left edge of the rows */
} BitsGeometry;

static void
bits_geometry (GtkWidget *widget, BitsGeometry *g)
{
        GdkFont *font = widget->style->font;
        gint width = widget->allocation.width;

        g->cell = MAX (gdk_char_width (font, '0'),
                       gdk_char_width (font, '1')) + 2;
        g->line = font->ascent + font->descent;
        g->row = 2 * g->line + BITS_PAD;
        /* 32 bits and 7 gaps between nibbles, each a bit wide */
        g->x = (width - 39 * g->cell) / 2;
}

/* where bit b is drawn */
static void
bit_position (const BitsGeometry *g, guint b, gint *x, gint *y)
{
        gint pos = 31 - (b % 32);

        *x = g->x + (pos + pos / 4) * g->cell;
        *y = BITS_PAD + (b >= 32 ? 0 : g->row);
}

static void
bits_style_set (GtkWidget *widget, GtkStyle *previous G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
        BitsGeometry g;

        bits_geometry (widget, &g);
        gtk_drawing_area_size (GTK_DRAWING_AREA (widget),
                               39 * g.cell + 2 * BITS_PAD,
                               2 * g.row + BITS_PAD);
}

static gint
bits_expose (GtkWidget *widget, GdkEventExpose *event G_GNUC_UNUSED, gpointer data)
{
        static const guint numbered[] = { 63, 47, 32, 31, 15, 0 };
        GtkCalc *calc = GTK_CALC (data);
        GdkFont *font = widget->style->font;
        GdkGC *on = widget->style->fg_gc[GTK_STATE_NORMAL];
        GdkGC *off = widget->style->fg_gc[GTK_STATE_INSENSITIVE];
        guint64 word = calc->_priv->iresult;
        BitsGeometry g;
        gchar text[4];
        guint b, i;
        gint x, y;

        bits_geometry (widget, &g);

        gdk_draw_rectangle (widget->window,
                            widget->style->bg_gc[GTK_STATE_NORMAL], TRUE,
                            0, 0,
                            widget->allocation.width,
                            widget->allocation.height);

        for (b = 0; b < 64; b++) {
                text[0] = (word >> b) & 1 ? '1' : '0';
                bit_position (&g, b, &x, &y);
                x += (g.cell - gdk_char_width (font, text[0])) / 2;
                gdk_draw_text (widget->window, font,
                               b < calc->_priv->width ? on : off,
                               x, y + font->ascent, text, 1);
        }

        /* the numbers of the first and last bits of each half row,
         * the last one of a row kept to the right of its bit */
        for (i = 0; i < sizeof (numbered) / sizeof (numbered[0]); i++) {
                b = numbered[i];
                g_snprintf (text, sizeof (text), "%u", b);
                bit_position (&g, b, &x, &y);
                if (b % 32 == 0)
                        x += g.cell - gdk_string_width (font, text);
                gdk_draw_string (widget->window, font, off,
                                 x, y + g.line + font->ascent, text);
        }

        return TRUE;
}

static gint
bits_button_press (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
        GtkCalc *calc = GTK_CALC (data);
        BitsGeometry g;
        gint x, y;
        guint b;

        if (event->button != 1 || calc->_priv->error)
                return FALSE;

        bits_geometry (widget, &g);

        for (b = 0; b < calc->_priv->width; b++) {
                bit_position (&g, b, &x, &y);
                if (event->x >= x && event->x < x + g.cell &&
                    event->y >= y && event->y < y + g.line) {
                        set_word_to (calc, calc->_priv->iresult ^
                                     ((guint64)1 << b));
                        return TRUE;
                }
        }

        return FALSE;
}

static void
create_button(GtkCalc *gc, GtkWidget *table,
              const CalculatorButton (*keypad)[5], int x, int y)
{
        const CalculatorButton *but = &keypad[y][x];
        GtkWidget *w;
        int i;

        if(!but->name)
                return;

        if (but->toggle) {
                w = gtk_toggle_button_new_with_label(_(but->name));
                if (but->signal_func == signed_toggle)
                        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),
                                                     gc->_priv->is_signed);
                gtk_signal_connect(GTK_OBJECT(w), "toggled",
                                   GTK_SIGNAL_FUNC(but->signal_func),
                                   (gpointer) but);
                if (but->signal_func == invert_toggle)
                        gc->_priv->invert_button = w;
        } else {
                w = gtk_button_new_with_label(_(but->name));
                gtk_signal_connect(GTK_OBJECT(w), "clicked",
                                   GTK_SIGNAL_FUNC(but->signal_func),
                                   (gpointer) but);
        }

        for(i=0;but->keys[i]!=0;i++) {
                gtk_widget_add_accelerator(w, "clicked",
                                           gc->_priv->accel,
                                           but->keys[i], 0,
                                           GTK_ACCEL_VISIBLE);
                gtk_widget_add_accelerator(w, "clicked",
                                           gc->_priv->accel,
                                           but->keys[i],
                                           GDK_SHIFT_MASK,
                                           GTK_ACCEL_VISIBLE);
                gtk_widget_add_accelerator(w, "clicked",
                                           gc->_priv->accel,
                                           but->keys[i],
                                           GDK_LOCK_MASK,
                                           GTK_ACCEL_VISIBLE);
        }
        gtk_object_set_user_data(GTK_OBJECT(w),gc);
        gtk_widget_show(w);
        gtk_table_attach(GTK_TABLE(table), w,
                         x, x+1, y, y+1,
                         GTK_FILL | GTK_EXPAND |
                         GTK_SHRINK,
                         GTK_FILL | GTK_EXPAND | GTK_SHRINK, 2, 2);

        /* remember the buttons whose labels change, and the digits */
        if(but->signal_func == drg_toggle)
                gc->_priv->drg_button = w;
        else if(but->signal_func == base_cycle)
                gc->_priv->base_button = w;
        else if(but->signal_func == width_cycle)
                gc->_priv->width_button = w;
        else if(but->signal_func == add_digit &&
                gc->_priv->layout == GTK_CALC_PROGRAMMER) {
                char c = but->name[0];
                gc->_priv->digit_buttons[c <= '9' ? c - '0' : c - 'A' + 10] = w;
        }
}

/* Put up the keypad, replacing the one there was. */
static void
build_keypad (GtkCalc *gc, const CalculatorButton (*keypad)[5])
{
        GtkWidget *table;
        gint x, y;

        if (gc->_priv->table)
                gtk_widget_destroy (gc->_priv->table);
        gc->_priv->invert_button = NULL;
        gc->_priv->drg_button = NULL;
        gc->_priv->base_button = NULL;
        gc->_priv->width_button = NULL;
        memset (gc->_priv->digit_buttons, 0,
                sizeof (gc->_priv->digit_buttons));

        table = gtk_table_new (8, 5, TRUE);
        gtk_widget_show (table);
        gtk_box_pack_end (GTK_BOX (gc), table, TRUE, TRUE, 0);
        gc->_priv->table = table;

        for (x = 0; x < 5; x++) {
                for (y = 0; y < 8; y++) {
                        create_button (gc, table, keypad, x, y);
                }
        }

        setup_drg_label (gc);
        setup_base_keys (gc);
        setup_width_label (gc);
}

static void
gtk_calc_init (gpointer object, gpointer klass G_GNUC_UNUSED)
{
        GtkCalc *gc = object;

        gc->_priv = g_new0(GtkCalcPrivate, 1);

        gc->_priv->display = gtk_drawing_area_new ();
        gtk_drawing_area_size (GTK_DRAWING_AREA (gc->_priv->display), DISPLAY_LEN * FONT_WIDTH, FONT_HEIGHT);

        gtk_signal_connect (GTK_OBJECT (gc->_priv->display), "realize",
                            GTK_SIGNAL_FUNC (display_realize),
                            gc);
        gtk_signal_connect (GTK_OBJECT (gc->_priv->display), "unrealize",
                            GTK_SIGNAL_FUNC (display_unrealize),
                            gc);
        gtk_signal_connect (GTK_OBJECT (gc->_priv->display), "expose_event",
                            GTK_SIGNAL_FUNC (display_expose),
                            gc);

        gtk_box_pack_start (GTK_BOX (gc), gc->_priv->display, FALSE, FALSE, 0);
        gtk_widget_show (gc->_priv->display);

        /* shown with the programmer's keypad */
        gc->_priv->bits = gtk_drawing_area_new ();
        gtk_widget_set_events (gc->_priv->bits,
                               GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK);
        gtk_signal_connect (GTK_OBJECT (gc->_priv->bits), "style_set",
                            GTK_SIGNAL_FUNC (bits_style_set), gc);
        gtk_signal_connect (GTK_OBJECT (gc->_priv->bits), "expose_event",
                            GTK_SIGNAL_FUNC (bits_expose), gc);
        gtk_signal_connect (GTK_OBJECT (gc->_priv->bits), "button_press_event",
                            GTK_SIGNAL_FUNC (bits_button_press), gc);
        bits_style_set (gc->_priv->bits, NULL, gc);
        gtk_box_pack_start (GTK_BOX (gc), gc->_priv->bits, FALSE, FALSE,
                            CGCALC_PAD_SMALL);

        gc->_priv->stack = NULL;
        gc->_priv->result = 0;
        strcpy(gc->_priv->result_string," 0");
        gc->_priv->memory = 0;
        gc->_priv->mode = GTK_CALC_DEG;
        gc->_priv->invert = FALSE;
        gc->_priv->add_digit = TRUE;
        gc->_priv->accel = gtk_accel_group_new();

        gc->_priv->layout = GTK_CALC_SCIENTIFIC;
        gc->_priv->base = GTK_CALC_BASE_HEX;
        gc->_priv->width = 32;
        gc->_priv->is_signed = FALSE;

        build_keypad (gc, scientific_buttons);
}


/**
 * gtk_calc_new:
 *
 * Description: Creates a calculator widget, a window with all the common
 * buttons and functions found on a standard pocket calculator.
 *
 * Returns: Pointer to newly-created calculator widget.
 **/

GtkWidget *
gtk_calc_new (void)
{
        GtkCalc *gcalculator;

        gcalculator = gtk_type_new (gtk_calc_get_type ());

        return GTK_WIDGET (gcalculator);
}

static void
gtk_calc_destroy (GtkObject *object)
{
        GtkCalc *gc;

        /* remember, destroy can be run multiple times! */

        g_return_if_fail (object != NULL);
        g_return_if_fail (GTK_IS_CALC (object));

        gc = GTK_CALC (object);

        while(gc->_priv->stack)
                stack_pop(&gc->_priv->stack);

        if (GTK_OBJECT_CLASS (parent_class)->destroy)
                (* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}

static void
gtk_calc_finalize (GtkObject *object)
{
        GtkCalc *gc;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GTK_IS_CALC (object));

        gc = GTK_CALC (object);

        if(gc->_priv) {
                gtk_accel_group_unref (gc->_priv->accel);
                g_free(gc->_priv);
                gc->_priv = NULL;
        }

        if (GTK_OBJECT_CLASS (parent_class)->finalize)
                (* GTK_OBJECT_CLASS (parent_class)->finalize) (object);
}

/**
 * gtk_calc_get_result
 * @gc: Pointer to GNOME calculator widget
 *
 * Description:  Gets the value of the result buffer as a double.
 * This should read off whatever is currently on the display of the
 * calculator.
 *
 * Returns:  A double precision result.
 **/

gdouble
gtk_calc_get_result (GtkCalc *gc)
{
        g_return_val_if_fail (gc, 0.0);
        g_return_val_if_fail (GTK_IS_CALC (gc), 0.0);

        if (gc->_priv->layout == GTK_CALC_PROGRAMMER)
                return word_to_double (gc, gc->_priv->iresult);
        return gc->_priv->result;
}

/**
 * gtk_calc_get_accel_group
 * @gc: Pointer to GNOME calculator widget
 *
 * Description:  Gets the accelerator group which you can add to
 * the toplevel window or wherever you want to catch the keys for this
 * widget.
 *
 * Returns:  The accelerator group
 **/

GtkAccelGroup   *
gtk_calc_get_accel_group(GtkCalc *gc)
{
        g_return_val_if_fail (gc, NULL);
        g_return_val_if_fail (GTK_IS_CALC (gc), NULL);

        return gc->_priv->accel;
}
/**
 * gtk_calc_get_result_string:
 * @gc: Pointer to GNOME calculator widget
 *
 * Description:  Gets the internal string representation of the result.
 *
 * Returns:  Internal string pointer, do not free
 **/
const char *
gtk_calc_get_result_string(GtkCalc *gc)
{
        g_return_val_if_fail (gc, NULL);
        g_return_val_if_fail (GTK_IS_CALC (gc), NULL);

        return gc->_priv->result_string;
}



/**
 * gtk_calc_set_layout:
 * @gc: Pointer to GNOME calculator widget
 * @layout: the keypad to show
 *
 * Description:  Switches between the scientific keypad and the
 * programmer's. The number shown and the memory are carried across,
 * cut to a whole word going to the programmer's; an unfinished
 * calculation is dropped.
 **/
void
gtk_calc_set_layout (GtkCalc *gc, GtkCalcLayout layout)
{
        gdouble result = 0;
        guint64 iresult = 0;

        g_return_if_fail (gc != NULL);
        g_return_if_fail (GTK_IS_CALC (gc));

        if (layout == gc->_priv->layout)
                return;

        if (layout == GTK_CALC_PROGRAMMER) {
                if (!gc->_priv->error)
                        iresult = double_to_word (gc, gc->_priv->result);
                gc->_priv->imemory = double_to_word (gc, gc->_priv->memory);
        } else {
                if (!gc->_priv->error)
                        result = word_to_double (gc, gc->_priv->iresult);
                gc->_priv->memory = word_to_double (gc, gc->_priv->imemory);
        }

        while (gc->_priv->stack)
                stack_pop (&gc->_priv->stack);
        gc->_priv->error = FALSE;
        gc->_priv->invert = FALSE;
        gc->_priv->entry = 0;
        gc->_priv->entry_neg = FALSE;

        gc->_priv->layout = layout;
        if (layout == GTK_CALC_PROGRAMMER) {
                gtk_drawing_area_size (GTK_DRAWING_AREA (gc->_priv->display),
                                       PROG_DISPLAY_LEN * FONT_WIDTH,
                                       FONT_HEIGHT);
                gtk_widget_show (gc->_priv->bits);
                build_keypad (gc, programmer_buttons);
        } else {
                gtk_drawing_area_size (GTK_DRAWING_AREA (gc->_priv->display),
                                       DISPLAY_LEN * FONT_WIDTH, FONT_HEIGHT);
                gtk_widget_hide (gc->_priv->bits);
                build_keypad (gc, scientific_buttons);
        }

        gc->_priv->add_digit = TRUE;
        gc->_priv->result = result;
        gc->_priv->iresult = iresult;
        push_input (gc);
        set_result (gc);
        redisplay (gc);
}

GtkCalcLayout
gtk_calc_get_layout (GtkCalc *gc)
{
        g_return_val_if_fail (gc, GTK_CALC_SCIENTIFIC);
        g_return_val_if_fail (GTK_IS_CALC (gc), GTK_CALC_SCIENTIFIC);

        return gc->_priv->layout;
}

/**
 * gtk_calc_get_base:
 * @gc: Pointer to GNOME calculator widget
 *
 * Description:  Gets the base the programmer's keypad shows words in.
 *
 * Returns:  The base, whichever keypad is shown
 **/
GtkCalcBase
gtk_calc_get_base (GtkCalc *gc)
{
        g_return_val_if_fail (gc, GTK_CALC_BASE_DEC);
        g_return_val_if_fail (GTK_IS_CALC (gc), GTK_CALC_BASE_DEC);

        return gc->_priv->base;
}


/**
 * delete_digit:
 * @w:
 * @data:
 *
 * Handle the backspace event
 **/
static void
backspace_cb (GtkWidget *w, gpointer data)
{
        GtkCalc *gc = gtk_object_get_user_data(GTK_OBJECT(w));
        char *old_locale;
        gint length;

        g_return_if_fail (GTK_IS_CALC (gc));

        if(gc->_priv->error)
                return;

        unselect_invert(gc);

        if (gc->_priv->layout == GTK_CALC_PROGRAMMER) {
                if (!gc->_priv->add_digit) {
                        clear_calc (w, data);
                        return;
                }
                gc->_priv->entry /= base_radix (gc->_priv->base);
                if (gc->_priv->entry == 0)
                        gc->_priv->entry_neg = FALSE;
                update_entry (gc);
                return;
        }

        length = strlen (gc->_priv->result_string);

        /* The behaivor that I expect from the calculator when the backspace key
         * is pressed is :
         * - If i was typing a number, i expect to delete the last char
         * - If i have a result already, i expect to clear the calculator
         */
        if (!gc->_priv->add_digit) {
                clear_calc (w, data);
                return;
        }

        if (atof (gc->_priv->result_string) == 0.0)
                return;

        gc->_priv->result_string [length - 1] = 0;
        if (atof (gc->_priv->result_string) == 0.0) {
                gc->_priv->result_string [0] = ' ';
                gc->_priv->result_string [1] = '0';
                gc->_priv->result_string [2] = 0;
        }

        gc->_priv->result = atof (gc->_priv->result_string);
        if (gc->_priv->stack) {
                CalculatorStack *stack = gc->_priv->stack->data;
                if (stack->type == CALCULATOR_NUMBER)
                        stack->d.number = gc->_priv->result;
        }

        gtk_widget_queue_draw (gc->_priv->display);

        /* make sure get values in a consistent manner */
        old_locale = g_strdup (setlocale (LC_NUMERIC, NULL));
        setlocale (LC_NUMERIC, "C");
        sscanf(gc->_priv->result_string, "%lf", &gc->_priv->result);
        setlocale (LC_NUMERIC, old_locale);
        g_free (old_locale);
}

/*
 * This code adds the ability to bound other keys to certain functions
 * usefull for keys that can't be bound as an accelerator or functions
 * that do not have a button in the calculator. Chema
 */
typedef struct _GtkCalcExtraKeys GtkCalcExtraKeys;

struct _GtkCalcExtraKeys {
        guint key;
        CalculatorFunc signal_func;
        const char *digit;
};

static const GtkCalcExtraKeys extra_keys [] = {
        {GDK_BackSpace, backspace_cb, NULL},
        {GDK_Return,    no_func,      NULL},
        {GDK_KP_Enter,  no_func,      NULL},
        {GDK_Delete,    clear_calc,   NULL},
        {GDK_KP_Delete, add_digit,    "."},
        {GDK_KP_Left,   add_digit,    "4"},
        {GDK_KP_Right,  add_digit,    "6"},
        {GDK_KP_Up,     add_digit,    "8"},
        {GDK_KP_Down,   add_digit,    "2"}
};

/* The programmer's function keys, as the Windows calculator has them:
 * F5 to F8 for the bases, F12 and F2 to F4 for 64 bits down to 8. */
static const struct {
        guint key;
        gint base;
        guint width;
} programmer_keys[] = {
        { GDK_F5,  GTK_CALC_BASE_HEX, 0 },
        { GDK_F6,  GTK_CALC_BASE_DEC, 0 },
        { GDK_F7,  GTK_CALC_BASE_OCT, 0 },
        { GDK_F8,  GTK_CALC_BASE_BIN, 0 },
        { GDK_F12, -1, 64 },
        { GDK_F2,  -1, 32 },
        { GDK_F3,  -1, 16 },
        { GDK_F4,  -1, 8 }
};

static gboolean
event_cb (GtkWidget *widget, GdkEvent *event, gpointer calc)
{
        GtkCalc *gc;
        gint i, num;
        GdkEventKey *kevent;

        if (event->type != GDK_KEY_PRESS)
                return FALSE;

        kevent = (GdkEventKey *)event;

        gc = (GtkCalc *) calc;
        g_return_val_if_fail (GTK_IS_CALC (gc), FALSE);

        num = sizeof (programmer_keys) / sizeof (programmer_keys[0]);
        if (gc->_priv->layout == GTK_CALC_PROGRAMMER &&
            !(kevent->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK))) {
                for (i = 0; i < num; i++) {
                        if (kevent->keyval != programmer_keys[i].key)
                                continue;
                        if (programmer_keys[i].base >= 0)
                                set_base (gc, programmer_keys[i].base);
                        else
                                set_width (gc, programmer_keys[i].width);
                        return TRUE;
                }
        }

        num = sizeof (extra_keys) / sizeof (GtkCalcExtraKeys);

        for (i = 0; i < num; i++) {
                if (kevent->keyval == extra_keys[i].key) {
                        CalculatorButton but = { .name = NULL };
                        CalculatorFunc func = extra_keys [i].signal_func;
                        but.digit = extra_keys[i].digit;
                        (* func) (widget, &but);
                        return TRUE;
                }
        }

        return FALSE;
}


void
gtk_calc_bind_extra_keys (GtkCalc *gc,
                            GtkWidget *widget)
{
        g_return_if_fail (GTK_IS_CALC (gc));
        g_return_if_fail (GTK_IS_WIDGET (widget));

        gtk_object_set_user_data (GTK_OBJECT (widget), gc);

        gtk_signal_connect (GTK_OBJECT (widget), "event",
                            GTK_SIGNAL_FUNC (event_cb), gc);

}
