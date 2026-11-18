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

#ifndef __GTK_CALC_H__
#define __GTK_CALC_H__

#include <gdk/gdk.h>
#include <gtk/gtkvbox.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GTK_TYPE_CALC            (gtk_calc_get_type ())
#define GTK_CALC(obj)            (GTK_CHECK_CAST ((obj), GTK_TYPE_CALC, GtkCalc))
#define GTK_CALC_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), GTK_TYPE_CALC, GtkCalcClass))
#define GTK_IS_CALC(obj)         (GTK_CHECK_TYPE ((obj), GTK_TYPE_CALC))
#define GTK_IS_CALC_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), GTK_TYPE_CALC))


typedef struct _GtkCalc        GtkCalc;
typedef struct _GtkCalcPrivate GtkCalcPrivate;
typedef struct _GtkCalcClass   GtkCalcClass;

typedef enum {
        GTK_CALC_DEG,
        GTK_CALC_RAD,
        GTK_CALC_GRAD
} GtkCalcMode;

/* the keypad: the scientific one, or the programmer's, which works on
 * whole numbers of a given word size in a chosen base */
typedef enum {
        GTK_CALC_SCIENTIFIC,
        GTK_CALC_PROGRAMMER
} GtkCalcLayout;

/* in the order the base button steps through them */
typedef enum {
        GTK_CALC_BASE_HEX,
        GTK_CALC_BASE_DEC,
        GTK_CALC_BASE_OCT,
        GTK_CALC_BASE_BIN
} GtkCalcBase;

struct _GtkCalc {
        GtkVBox vbox;

        /*< private >*/
        GtkCalcPrivate *_priv;
};

struct _GtkCalcClass {
        GtkVBoxClass parent_class;

        void (* result_changed)(GtkCalc *gc,
                                gdouble result);
};


GtkType          gtk_calc_get_type      (void) G_GNUC_CONST;
GtkWidget       *gtk_calc_new           (void);

void             gtk_calc_clear         (GtkCalc *gc,
                                         const gboolean reset);
void             gtk_calc_set           (GtkCalc *gc,
                                         gdouble result);

gdouble          gtk_calc_get_result        (GtkCalc *gc);
GtkAccelGroup   *gtk_calc_get_accel_group   (GtkCalc *gc);
const char      *gtk_calc_get_result_string (GtkCalc *gc);

void             gtk_calc_set_layout        (GtkCalc *gc,
                                             GtkCalcLayout layout);
GtkCalcLayout    gtk_calc_get_layout        (GtkCalc *gc);
GtkCalcBase      gtk_calc_get_base          (GtkCalc *gc);

void             gtk_calc_bind_extra_keys   (GtkCalc *gc,
                                             GtkWidget *widget);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
