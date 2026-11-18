/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* Simple Double precision calculator using the GtkCalc widget
   Copyright (C) 1998 Free Software Foundation

   Author: George Lebl <jirka@5z.com>
*/

#include <config.h>

#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>

#include "cgcalc.h"
#include "gtkcalc.h"
#include "sr.h"

#include "icons/cgcalc.xpm"

/* values for selection info */
enum {
        TARGET_STRING,
        TARGET_TEXT,
        TARGET_COMPOUND_TEXT,
        TARGET_UTF8_STRING
};

static GtkWidget *app;
static GtkWidget *calc;
static gchar *copied_string = NULL;
static GdkPixmap *icon = NULL;
static GdkBitmap *icon_mask = NULL;

void
cgcalc_set_icon (GtkWidget *window)
{
        gtk_widget_realize (window);
        if (icon == NULL)
                icon = gdk_pixmap_create_from_xpm_d (window->window,
                                                     &icon_mask, NULL,
                                                     cgcalc_xpm);
        if (icon != NULL)
                gdk_window_set_icon (window->window, NULL, icon, icon_mask);
}

static void
about_destroy_cb (GtkObject *obj G_GNUC_UNUSED, GtkWidget **about)
{
        *about = NULL;
}

static void
about_cb (gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *widget G_GNUC_UNUSED)
{
        static GtkWidget *about = NULL;
        static const gchar *authors[] = {
                "George Lebl <jirka@5z.com>",
                NULL
        };
        GtkWidget *vbox, *credits, *label, *button;
        gchar *text;
        gint i;

        if (about != NULL)
        {
                gdk_window_show(about->window);
                gdk_window_raise(about->window);
                return;
        }

        about = gtk_dialog_new ();
        gtk_window_set_title (GTK_WINDOW (about), _("About CGCalc"));
        gtk_window_set_policy (GTK_WINDOW (about), FALSE, FALSE, TRUE);
        gtk_window_set_transient_for (GTK_WINDOW (about), GTK_WINDOW (app));
        gtk_window_position (GTK_WINDOW (about), GTK_WIN_POS_CENTER);
        gtk_signal_connect (GTK_OBJECT (about), "destroy",
                            GTK_SIGNAL_FUNC (about_destroy_cb), &about);

        vbox = gtk_vbox_new (FALSE, CGCALC_PAD_SMALL);
        gtk_container_border_width (GTK_CONTAINER (vbox), 2 * CGCALC_PAD);
        gtk_box_pack_start (GTK_BOX (GTK_DIALOG (about)->vbox), vbox,
                            TRUE, TRUE, 0);

        if (icon != NULL)
                gtk_box_pack_start (GTK_BOX (vbox),
                                    gtk_pixmap_new (icon, icon_mask),
                                    FALSE, FALSE, 0);

        text = g_strdup_printf ("CGCalc %s", PACKAGE_VERSION);
        label = gtk_label_new (text);
        g_free (text);
        gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

        label = gtk_label_new (_("Simple double precision calculator similar "
                                 "to xcalc"));
        gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

        /* the original authors beneath the original copyright */
        credits = gtk_vbox_new (FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), credits, FALSE, FALSE,
                            CGCALC_PAD_SMALL);
        label = gtk_label_new ("(C) 1998 the Free Software Foundation");
        gtk_box_pack_start (GTK_BOX (credits), label, FALSE, FALSE, 0);
        for (i = 0; authors[i]; i++) {
                label = gtk_label_new (authors[i]);
                gtk_box_pack_start (GTK_BOX (credits), label, FALSE, FALSE, 0);
        }

        label = gtk_label_new ("(C) 2026 The CGTK authors");
        gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

        button = gtk_button_new_with_label (_("OK"));
        GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
        gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
                                   GTK_SIGNAL_FUNC (gtk_widget_destroy),
                                   GTK_OBJECT (about));
        gtk_box_pack_start (GTK_BOX (GTK_DIALOG (about)->action_area), button,
                            TRUE, TRUE, 0);

        gtk_widget_show_all (about);
        gtk_widget_grab_default (button);
}

static void
quit_cb (gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *widget G_GNUC_UNUSED)
{
        gtk_main_quit ();
}

static gint
delete_cb (GtkWidget *widget G_GNUC_UNUSED, GdkEventAny *event G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
        gtk_main_quit ();
        return TRUE;
}

static void
layout_cb (gpointer data G_GNUC_UNUSED, guint action, GtkWidget *widget)
{
        /* the item turned off is told as well */
        if (GTK_CHECK_MENU_ITEM (widget)->active)
                gtk_calc_set_layout (GTK_CALC (calc), action);
}

/* Callback when the user toggles the selection. The result goes in
 * the clipboard as well as the primary selection, which is all X had
 * when this was written, and which Wayland may not have. A word from
 * the programmer's keypad is written as C would have it. */
static void
copy_contents (gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *widget G_GNUC_UNUSED)
{
        const gchar *prefix = "";
        gchar *result;

        result = g_strdup (gtk_calc_get_result_string (GTK_CALC (calc)));
        g_strstrip (result);

        if (gtk_calc_get_layout (GTK_CALC (calc)) == GTK_CALC_PROGRAMMER &&
            strcmp (result, "e") != 0) {
                switch (gtk_calc_get_base (GTK_CALC (calc))) {
                case GTK_CALC_BASE_HEX: prefix = "0x"; break;
                case GTK_CALC_BASE_OCT: prefix = "0";  break;
                case GTK_CALC_BASE_BIN: prefix = "0b"; break;
                default:                break;
                }
        }

        g_free (copied_string);
        copied_string = g_strconcat (prefix, result, NULL);
        g_free (result);
        gtk_selection_owner_set (app,
                                 GDK_SELECTION_PRIMARY,
                                 GDK_CURRENT_TIME);
        gtk_selection_owner_set (app,
                                 gdk_atom_intern ("CLIPBOARD", FALSE),
                                 GDK_CURRENT_TIME);
}


static void
selection_handle (GtkWidget *widget G_GNUC_UNUSED,
                  GtkSelectionData *selection_data,
                  guint info, guint time G_GNUC_UNUSED,
                  gpointer data G_GNUC_UNUSED)
{
        if (!copied_string)
                return;

        if (info == TARGET_STRING) {
                gtk_selection_data_set (selection_data,
                                        GDK_SELECTION_TYPE_STRING,
                                        8*sizeof(gchar),
                                        (guchar *)copied_string,
                                        strlen(copied_string));
        } else if (info == TARGET_UTF8_STRING) {
                /* only digits and signs: the same in any encoding */
                gtk_selection_data_set (selection_data,
                                        gdk_atom_intern ("UTF8_STRING", FALSE),
                                        8*sizeof(gchar),
                                        (guchar *)copied_string,
                                        strlen(copied_string));
        } else if ((info == TARGET_TEXT) || (info == TARGET_COMPOUND_TEXT)) {
                guchar *text;
                GdkAtom encoding;
                gint format;
                gint new_length;

                gdk_string_to_compound_text (copied_string, &encoding, &format,
                                             &text, &new_length);
                gtk_selection_data_set (selection_data, encoding, format,
                                        text, new_length);
                gdk_free_compound_text (text);
        }
}

/*
 * Menus. An entry is its parent's path, its label and what it does;
 * the label is translated, the path an item is found by is not.
 */
typedef struct _MenuEntry {
        const gchar *parent;
        const gchar *label;
        const gchar *accelerator;
        void (*callback)(gpointer, guint, GtkWidget *);
        guint action;
        const gchar *item_type;
} MenuEntry;

static const MenuEntry menu_entries[] = {
        { "",      N_("_File"),     NULL,         NULL,          0, "<Branch>" },
        { "/File", N_("E_xit"),     "<control>Q", quit_cb,       0, NULL },
        { "",      N_("_Edit"),     NULL,         NULL,          0, "<Branch>" },
        { "/Edit", N_("_Copy"),     "<control>C", copy_contents, 0, NULL },
        { "",      N_("_View"),     NULL,         NULL,          0, "<Branch>" },
        { "/View", N_("_Scientific"), NULL,       layout_cb,
          GTK_CALC_SCIENTIFIC, "<RadioItem>" },
        { "/View", N_("_Programmer"), NULL,       layout_cb,
          GTK_CALC_PROGRAMMER, "/View/Scientific" },
        { "",      N_("_Help"),     NULL,         NULL,          0, "<Branch>" },
        { "/Help", N_("_About..."), NULL,         about_cb,      0, NULL },
};

/* only the last part of a path is a label */
static gchar *
menu_translate (const gchar *path, gpointer data G_GNUC_UNUSED)
{
        const gchar *label = strrchr (path, '/');

        return _(label ? label + 1 : path);
}

static GtkWidget *
create_menus (GtkAccelGroup *accel_group)
{
        GtkItemFactory *ifactory;
        GtkItemFactoryEntry entry;
        gchar *path;
        guint i;

        ifactory = gtk_item_factory_new (GTK_TYPE_MENU_BAR, "<main>",
                                         accel_group);
        gtk_item_factory_set_translate_func (ifactory, menu_translate,
                                             NULL, NULL);

        for (i = 0; i < sizeof (menu_entries) / sizeof (menu_entries[0]); i++) {
                const MenuEntry *me = &menu_entries[i];

                path = g_strconcat (me->parent, "/", me->label, NULL);
                entry.path = path;
                entry.accelerator = (gchar *)me->accelerator;
                entry.callback = (GtkItemFactoryCallback)me->callback;
                entry.callback_action = me->action;
                entry.item_type = (gchar *)me->item_type;
                gtk_item_factory_create_item (ifactory, &entry, NULL, 1);
                g_free (path);
        }

        return gtk_item_factory_get_widget (ifactory, "<main>");
}

static void
usage (const gchar *argv0)
{
        printf (_("Usage: %s [OPTION...]\n"), argv0);
        printf ("\n");
        printf ("  -analog      %s\n", _("Run in analog (slide rule) mode"));
        printf (_("  --help       show this help and exit\n"));
        printf (_("  --version    show the version and exit\n"));
        printf ("\n");
        printf (_("The usual GTK options, such as --display, are also understood.\n"));
}

int
main(int argc, char *argv[])
{
        static GtkTargetEntry targets[] = {
                { "STRING", 0, TARGET_STRING },
                { "TEXT",   0, TARGET_TEXT },
                { "COMPOUND_TEXT", 0, TARGET_COMPOUND_TEXT },
                { "UTF8_STRING", 0, TARGET_UTF8_STRING }
        };
        static gint n_targets = sizeof(targets) / sizeof(targets[0]);
        GtkAccelGroup *accel_group;
        GtkWidget *vbox, *menubar;
        gboolean analog = FALSE;
        int i;

        /* Initialize the i18n stuff */
#ifdef ENABLE_NLS
        bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
        textdomain (GETTEXT_PACKAGE);
#endif

        gtk_set_locale ();

        /* these need no display */
        for (i = 1; i < argc; i++) {
                if (strcmp (argv[i], "--help") == 0 ||
                    strcmp (argv[i], "-h") == 0) {
                        usage (argv[0]);
                        return 0;
                }
                else if (strcmp (argv[i], "--version") == 0) {
                        printf ("CGCalc %s\n", PACKAGE_VERSION);
                        return 0;
                }
        }

        gtk_init (&argc, &argv);
        /* every window's icon, where the window manager takes one in
         * colour; cgcalc_set_icon() gives the pixmap for where it does
         * not, or the icon is not installed where icons are looked for */
        gtk_window_set_default_icon_name ("cgcalc");

        /* what is left once GTK has taken its own */
        for (i = 1; i < argc; i++) {
                if (strcmp (argv[i], "-analog") == 0 ||
                    strcmp (argv[i], "--analog") == 0)
                        analog = TRUE;
                else {
                        fprintf (stderr, _("%s: unknown option %s\n"),
                                 argv[0], argv[i]);
                        usage (argv[0]);
                        return 1;
                }
        }

        if (analog) {
                run_slide_rule ();
                return 0;
        }

        app = gtk_window_new (GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title (GTK_WINDOW (app), "CGCalc");
        gtk_window_set_wmclass (GTK_WINDOW (app), "cgcalc", "Cgcalc");
        /* shrinking back when the programmer's keypad goes */
        gtk_window_set_policy (GTK_WINDOW (app), FALSE, TRUE, TRUE);

        gtk_signal_connect(GTK_OBJECT(app), "delete_event",
                        GTK_SIGNAL_FUNC(delete_cb), NULL);

        vbox = gtk_vbox_new (FALSE, 0);
        gtk_container_add (GTK_CONTAINER (app), vbox);
        gtk_widget_show (vbox);

        /*set up the menu*/
        accel_group = gtk_accel_group_new ();
        gtk_window_add_accel_group (GTK_WINDOW (app), accel_group);
        menubar = create_menus (accel_group);
        gtk_box_pack_start (GTK_BOX (vbox), menubar, FALSE, FALSE, 0);
        gtk_widget_show (menubar);

        calc = gtk_calc_new();
        gtk_calc_bind_extra_keys (GTK_CALC (calc), GTK_WIDGET (app));
        gtk_container_border_width (GTK_CONTAINER (calc), CGCALC_PAD_SMALL);
        gtk_box_pack_start (GTK_BOX (vbox), calc, TRUE, TRUE, 0);
        gtk_widget_show(calc);

        gtk_selection_add_targets (GTK_WIDGET (app), GDK_SELECTION_PRIMARY,
                                   targets, n_targets);
        gtk_selection_add_targets (GTK_WIDGET (app),
                                   gdk_atom_intern ("CLIPBOARD", FALSE),
                                   targets, n_targets);

        gtk_signal_connect(GTK_OBJECT(app), "selection_get",
                           GTK_SIGNAL_FUNC(selection_handle), NULL);

        /* add calculator accel table to our window*/
        gtk_window_add_accel_group(GTK_WINDOW(app),
                                   gtk_calc_get_accel_group(GTK_CALC(calc)));

        cgcalc_set_icon (app);
        gtk_widget_show(app);

        gtk_main ();

        return 0;
}
