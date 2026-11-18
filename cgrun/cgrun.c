/* cgrun.c - a box to start programs from

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

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include "cgrun.h"

enum {
    WIDTH = 440,
    HEIGHT = 220, /* of the list, when it is shown */
    NAMES = 180,  /* the width of the names */
    KEPT = 50     /* command lines remembered */
};

/* A command line that was typed and ran. */
typedef struct {
    gchar *line, *key;
    gboolean terminal;
    gint count;
} Cmd;

/* A row of the list: a program or a command line, and how it stands. */
typedef struct {
    App *app;
    Cmd *cmd;
    gint score, count;
} Row;

static GtkWidget *win, *combo, *entry, *term, *more, *pane, *list, *said;
static GList *apps, *cmds;  /* cmds: the last that ran first */
static GKeyFile *past;      /* what ran before, and how often */
static gboolean listed;     /* the list of programs is shown */
static gboolean ours;       /* the terminal box is as a line that ran had it */
static gboolean setting;    /* and is being set so, not by hand */
static gboolean byhand;     /* it was set by hand, and is left alone */
static glong had;           /* how long the line was before a key, in characters */

/*
 * What ran before is kept in a key file: a group Count with a number
 * for each program by its id, and a group for each command line, the
 * last that ran first, with its Line, its Count, and whether it ran in
 * a Terminal. It is the state of the thing and not a setting.
 */
static gchar *
past_file(void)
{
    const gchar *state = g_getenv("XDG_STATE_HOME");

    if (state && g_path_is_absolute(state))
        return g_build_filename(state, "cgrun", "history", NULL);
    return g_build_filename(g_get_home_dir(), ".local", "state", "cgrun", "history", NULL);
}

static void
past_load(void)
{
    gchar *file = past_file(), group[32], *line;
    gint i;

    past = g_key_file_new();
    g_key_file_load_from_file(past, file, G_KEY_FILE_NONE, NULL);
    for (i = 0; i < KEPT; i++) {
        g_snprintf(group, sizeof group, "Command %d", i);
        if (!(line = g_key_file_get_string(past, group, "Line", NULL)))
            break;
        if (*line) {
            Cmd *cmd = g_new0(Cmd, 1);

            cmd->line = line;
            cmd->key = text_key(line);
            cmd->count = g_key_file_get_integer(past, group, "Count", NULL);
            cmd->terminal = g_key_file_get_boolean(past, group, "Terminal", NULL);
            cmds = g_list_append(cmds, cmd);
        } else
            g_free(line);
    }
    g_free(file);
}

static void
past_save(void)
{
    gchar *file = past_file(), *dir = g_path_get_dirname(file), group[32];
    GList *l;
    gint i;

    for (i = 0; i < KEPT; i++) {
        g_snprintf(group, sizeof group, "Command %d", i);
        g_key_file_remove_group(past, group, NULL);
    }
    for (l = cmds, i = 0; l && i < KEPT; l = l->next, i++) {
        Cmd *cmd = l->data;

        g_snprintf(group, sizeof group, "Command %d", i);
        g_key_file_set_string(past, group, "Line", cmd->line);
        g_key_file_set_integer(past, group, "Count", cmd->count);
        if (cmd->terminal)
            g_key_file_set_boolean(past, group, "Terminal", TRUE);
    }
    if (g_mkdir_with_parents(dir, 0700) == 0)
        g_key_file_save_to_file(past, file, NULL);
    g_free(dir);
    g_free(file);
}

static gint
counted(const App *app)
{
    return g_key_file_get_integer(past, "Count", app->id, NULL);
}

/*
 * Something has run and counts for one more: a program, a line that
 * was typed, or both, when the line was a program's name.
 */
static void
past_add(App *app, const gchar *line, gboolean terminal)
{
    Cmd *cmd = NULL;
    GList *l;

    if (app)
        g_key_file_set_integer(past, "Count", app->id, counted(app) + 1);
    if (line) {
        for (l = cmds; l && !cmd; l = l->next)
            if (!strcmp(((Cmd *) l->data)->line, line))
                cmd = l->data;
        if (cmd)
            cmds = g_list_remove(cmds, cmd);
        else {
            cmd = g_new0(Cmd, 1);
            cmd->line = g_strdup(line);
            cmd->key = text_key(line);
        }
        cmd->count++;
        cmd->terminal = terminal;
        cmds = g_list_prepend(cmds, cmd);
    }
    past_save();
}

/* The program that is called this, capitals aside; of several the most started. */
static App *
app_keyed(const gchar *key)
{
    App *found = NULL;
    gint most = -1, count;
    GList *l;

    for (l = apps; l; l = l->next)
        if (!strcmp(((App *) l->data)->key, key) && (count = counted(l->data)) > most) {
            found = l->data;
            most = count;
        }
    return found;
}

static const gchar *
row_name(const Row *row)
{
    return row->app ? row->app->name : row->cmd->line;
}

static const gchar *
row_key(const Row *row)
{
    return row->app ? row->app->key : row->cmd->key;
}

/* The better met first, then the more often run, then by name. */
static gint
row_cmp(gconstpointer a, gconstpointer b)
{
    const Row *x = a, *y = b;

    if (x->score != y->score)
        return y->score - x->score;
    if (x->count != y->count)
        return y->count - x->count;
    return g_utf8_collate(row_key(x), row_key(y));
}

/* A line that is only a program's name is not listed beside the program. */
static GList *
row_add(GList *rows, App *app, Cmd *cmd, const gchar *want)
{
    gint score = app ? text_match(app->key, app->more, want) : text_match(cmd->key, NULL, want);
    Row *row;

    if (!score || (cmd && app_keyed(cmd->key)))
        return rows;
    row = g_new0(Row, 1);
    row->app = app;
    row->cmd = cmd;
    row->score = score;
    row->count = app ? counted(app) : cmd->count;
    return g_list_prepend(rows, row);
}

/* The list is what there is that meets what is typed. */
static void
refill(void)
{
    gchar *typed = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(entry))));
    gchar *want = text_key(typed), *text[2];
    GList *rows = NULL, *l;

    for (l = apps; l; l = l->next)
        rows = row_add(rows, l->data, NULL, want);
    for (l = cmds; l; l = l->next)
        rows = row_add(rows, NULL, l->data, want);
    rows = g_list_sort(rows, row_cmp);

    gtk_clist_freeze(GTK_CLIST(list));
    gtk_clist_clear(GTK_CLIST(list));
    for (l = rows; l; l = l->next) {
        Row *row = l->data;

        text[0] = (gchar *) row_name(row);
        text[1] = row->app && row->app->about ? row->app->about : "";
        gtk_clist_set_row_data_full(GTK_CLIST(list), gtk_clist_append(GTK_CLIST(list), text), row, g_free);
    }
    gtk_clist_thaw(GTK_CLIST(list));
    if (rows)
        gtk_clist_select_row(GTK_CLIST(list), 0, 0);
    g_list_free(rows);
    g_free(want);
    g_free(typed);
}

/*
 * The line has changed. What was said of the last one is no longer
 * so; the list, if it is shown, is of what meets this one; and if
 * this one ran before, the terminal box is as it was then, until the
 * line is another. Once the box is set by hand it is left as it is.
 */
static void
changed(GtkWidget *widget, gpointer data)
{
    const gchar *line = gtk_entry_get_text(GTK_ENTRY(entry));
    Cmd *cmd = NULL;
    GList *l;

    gtk_widget_hide(said);
    if (listed)
        refill();
    for (l = cmds; l && !cmd; l = l->next)
        if (!strcmp(((Cmd *) l->data)->line, line))
            cmd = l->data;
    if (!byhand && (cmd || ours)) {
        setting = TRUE;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(term), cmd && cmd->terminal);
        setting = FALSE;
        ours = cmd != NULL;
    }
}

/* The box set by hand stays as it is set, and typing goes on in the line. */
static void
term_toggled(GtkWidget *widget, gpointer data)
{
    if (setting)
        return;
    byhand = TRUE;
    gtk_widget_grab_focus(entry);
}

/*
 * What is left of a name or a line that begins with what was typed,
 * capitals aside, or NULL: key is what was typed and n the characters
 * in it.
 */
static const gchar *
rest_of(const gchar *whole, const gchar *key, glong n)
{
    const gchar *rest;
    gchar *head, *begun;
    gboolean same;

    if (!g_utf8_validate(whole, -1, NULL) || g_utf8_strlen(whole, -1) <= n)
        return NULL;
    rest = g_utf8_offset_to_pointer(whole, n);
    head = g_strndup(whole, rest - whole);
    begun = text_key(head);
    same = !strcmp(begun, key);
    g_free(begun);
    g_free(head);
    return same ? rest : NULL;
}

/*
 * With the list shut, what is typed is finished from what it could
 * be: a line that ran before, the latest first, and failing that the
 * name of a program, the most started first. The rest is put after
 * what was typed and is selected, so that typing on replaces it and
 * BackSpace takes it away.
 */
static void
complete(void)
{
    gchar *key = text_key(gtk_entry_get_text(GTK_ENTRY(entry)));
    glong n = g_utf8_strlen(gtk_entry_get_text(GTK_ENTRY(entry)), -1);
    const gchar *rest = NULL, *r;
    gint most = -1, count, at = n;
    App *best = NULL;
    GList *l;

    for (l = cmds; l && !rest; l = l->next)
        rest = rest_of(((Cmd *) l->data)->line, key, n);
    for (l = rest ? NULL : apps; l; l = l->next) {
        App *app = l->data;

        if ((r = rest_of(app->name, key, n)) && ((count = counted(app)) > most ||
            (count == most && strcmp(app->key, best->key) < 0))) {
            rest = r;
            best = app;
            most = count;
        }
    }
    if (rest) {
        gtk_editable_insert_text(GTK_EDITABLE(entry), rest, strlen(rest), &at);
        gtk_editable_set_position(GTK_EDITABLE(entry), n);
        gtk_editable_select_region(GTK_EDITABLE(entry), n, -1);
    }
    g_free(key);
}

static gint
chosen(void)
{
    GList *sel = GTK_CLIST(list)->selection;

    return listed && sel ? GPOINTER_TO_INT(sel->data) : -1;
}

static void
move(gint by)
{
    gint rows = GTK_CLIST(list)->rows, row = CLAMP(chosen() + by, 0, rows - 1);

    if (!rows)
        return;
    gtk_clist_select_row(GTK_CLIST(list), row, 0);
    if (gtk_clist_row_is_visible(GTK_CLIST(list), row) != GTK_VISIBILITY_FULL)
        gtk_clist_moveto(GTK_CLIST(list), row, 0, by > 0 ? 1.0 : 0.0, 0.0);
}

/*
 * Runs something, and goes if it started; if it did not, stays and
 * says why. With the list shown it is the row that is chosen, or,
 * with as_typed or no row, the line as a command line. With the list
 * shut it is the line: the program of that name if there is one, and
 * a command line if not. It is in a terminal if that is asked for
 * here, by the box, or by the program or the line itself.
 */
static void
go(gboolean as_typed, gboolean terminal)
{
    Row *row = as_typed || chosen() < 0 ? NULL : gtk_clist_get_row_data(GTK_CLIST(list), chosen());
    gchar *line = NULL, *key, **argv;
    App *app = row ? row->app : NULL;
    GError *error = NULL;

    terminal = terminal || GTK_TOGGLE_BUTTON(term)->active;
    if (!app) {
        line = g_strstrip(g_strdup(row ? row->cmd->line : gtk_entry_get_text(GTK_ENTRY(entry))));
        if (!*line) {
            g_free(line);
            return;
        }
        if (row)
            terminal = terminal || row->cmd->terminal;
        else if (!listed && !as_typed) {
            key = text_key(line);
            app = app_keyed(key);
            g_free(key);
        }
    }
    if (app) {
        argv = app_argv(app, &error);
        terminal = terminal || app->terminal;
    } else
        argv = line_argv(line, &error);
    if (argv && run_argv(argv, app ? app->dir : NULL, terminal, &error)) {
        past_add(app, line, terminal);
        gtk_main_quit();
    } else {
        gtk_label_set_text(GTK_LABEL(said), error ? error->message : "");
        gtk_widget_show(said);
        g_clear_error(&error);
    }
    g_strfreev(argv);
    g_free(line);
}

/*
 * The keys, before the line has them. With the list shown the arrows
 * choose in it; with it shut they are left to the line, which goes
 * through what ran before with them.
 */
static gint
key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    GtkCList *clist = GTK_CLIST(list);
    gint page = MAX(clist->clist_window_height / (clist->row_height + 1) - 1, 1), by = 0;
    GtkEditable *line = GTK_EDITABLE(entry);

    /* what is selected does not count: a key that is typed takes its place */
    had = g_utf8_strlen(gtk_entry_get_text(GTK_ENTRY(entry)), -1);
    if (line->has_selection)
        had -= ABS((glong) line->selection_end_pos - (glong) line->selection_start_pos);
    switch (event->keyval) {
    case GDK_Escape:
        gtk_main_quit();
        break;
    case GDK_Return: case GDK_KP_Enter:
        go((event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK)) != 0,
           (event->state & GDK_CONTROL_MASK) != 0);
        break;
    case GDK_Up: case GDK_KP_Up:
        by = -1;
        break;
    case GDK_Down: case GDK_KP_Down:
        by = 1;
        break;
    case GDK_Page_Up: case GDK_KP_Page_Up:
        by = -page;
        break;
    case GDK_Page_Down: case GDK_KP_Page_Down:
        by = page;
        break;
    default:
        return FALSE;
    }
    if (by && !listed)
        return FALSE;
    if (by)
        move(by);
    gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event");
    return TRUE;
}

/* A key, after the line has had it: if it made the line longer at its end, it was typed. */
static gint
key_pressed(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    const gchar *line = gtk_entry_get_text(GTK_ENTRY(entry));

    if (!listed && g_utf8_strlen(line, -1) > had && !(event->state & GDK_MOD1_MASK) &&
        event->keyval != GDK_Up && event->keyval != GDK_KP_Up &&
        event->keyval != GDK_Down && event->keyval != GDK_KP_Down &&
        gtk_editable_get_position(GTK_EDITABLE(entry)) == g_utf8_strlen(line, -1))
        complete();
    return FALSE;
}

static void
row_pressed(GtkWidget *widget, gint row, gint column, GdkEventButton *event, gpointer data)
{
    if (event && event->type == GDK_2BUTTON_PRESS)
        go(FALSE, FALSE);
}

static void
run_clicked(GtkWidget *widget, gpointer data)
{
    go(FALSE, FALSE);
}

/* The list is shown or shut, and the button says which way it would go. */
static void
list_show(gboolean show)
{
    gchar *label = g_strconcat(_("Programs"), show ? " <<" : " >>", NULL);

    listed = show;
    gtk_label_set_text(GTK_LABEL(GTK_BIN(more)->child), label);
    g_free(label);
    if (show) {
        refill();
        gtk_widget_show(pane);
    } else
        gtk_widget_hide(pane);
}

static void
more_clicked(GtkWidget *widget, gpointer data)
{
    list_show(!listed);
    gtk_widget_grab_focus(entry);
}

/*
 * There is one cgrun on a display: one that starts while another is
 * there takes its place, so the key that starts it always brings it
 * to where the user is. The one that is there holds a lock on a file
 * in the runtime directory, and a lock tells whose it is; that one is
 * told to end, and the lock is had when it has. The file stays open.
 */
static void
alone(void)
{
    const gchar *display = g_getenv("WAYLAND_DISPLAY");
    gchar *name, *file;
    struct flock lock;
    gint fd, i;

    if (!display && !(display = g_getenv("DISPLAY")))
        display = "";
    name = g_strdelimit(g_strconcat("cgrun-", display, ".lock", NULL), "/", '_');
    file = g_build_filename(g_get_user_runtime_dir(), name, NULL);
    fd = open(file, O_RDWR | O_CREAT, 0600);
    g_free(file);
    g_free(name);
    if (fd < 0)
        return;
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    for (i = 0; i < 100; i++) {
        memset(&lock, 0, sizeof lock);
        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        if (fcntl(fd, F_SETLK, &lock) == 0)
            return;
        if (fcntl(fd, F_GETLK, &lock) == 0 && lock.l_type != F_UNLCK && lock.l_pid > 0)
            kill(lock.l_pid, SIGTERM);
        usleep(10000);
    }
}

static GtkWidget *
button(GtkWidget *box, const gchar *label, GtkSignalFunc clicked)
{
    GtkWidget *b = gtk_button_new_with_label(label);

    GTK_WIDGET_SET_FLAGS(b, GTK_CAN_DEFAULT);
    gtk_signal_connect(GTK_OBJECT(b), "clicked", clicked, NULL);
    gtk_box_pack_start(GTK_BOX(box), b, TRUE, TRUE, 0);
    return b;
}

/*
 * The window, as the Run box of old: a word on what it is for, the
 * line, with what ran before to be let down from it, and Run and
 * Close; and a third button that opens the list of programs beneath.
 */
static void
build(void)
{
    GtkWidget *box, *row, *label, *buttons, *run, *scrolled;
    GList *lines = NULL, *l;

    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), _("Run"));
    gtk_window_set_wmclass(GTK_WINDOW(win), "cgrun", "Cgrun");
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);
    gtk_window_set_policy(GTK_WINDOW(win), FALSE, FALSE, TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(win), 10);
    gtk_signal_connect(GTK_OBJECT(win), "destroy", GTK_SIGNAL_FUNC(gtk_main_quit), NULL);
    gtk_signal_connect(GTK_OBJECT(win), "key_press_event", GTK_SIGNAL_FUNC(key_press), NULL);

    box = gtk_vbox_new(FALSE, 8);
    gtk_widget_set_usize(box, WIDTH, -1);
    gtk_container_add(GTK_CONTAINER(win), box);

    label = gtk_label_new(_("Type the name of a program, or a command line."));
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

    row = gtk_hbox_new(FALSE, 8);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
    label = gtk_label_new(_("Command:"));
    gtk_box_pack_start(GTK_BOX(row), label, FALSE, FALSE, 0);
    combo = gtk_combo_new();
    gtk_combo_disable_activate(GTK_COMBO(combo));
    gtk_combo_set_use_arrows_always(GTK_COMBO(combo), TRUE);
    gtk_combo_set_case_sensitive(GTK_COMBO(combo), TRUE);
    for (l = cmds; l; l = l->next)
        lines = g_list_append(lines, ((Cmd *) l->data)->line);
    if (lines)
        gtk_combo_set_popdown_strings(GTK_COMBO(combo), lines);
    else
        gtk_widget_set_sensitive(GTK_COMBO(combo)->button, FALSE);
    g_list_free(lines);
    entry = GTK_COMBO(combo)->entry;
    gtk_entry_set_text(GTK_ENTRY(entry), "");
    gtk_signal_connect(GTK_OBJECT(entry), "changed", GTK_SIGNAL_FUNC(changed), NULL);
    gtk_signal_connect_after(GTK_OBJECT(entry), "key_press_event", GTK_SIGNAL_FUNC(key_pressed), NULL);
    gtk_box_pack_start(GTK_BOX(row), combo, TRUE, TRUE, 0);

    term = gtk_check_button_new_with_label(_("Run in a terminal"));
    gtk_signal_connect(GTK_OBJECT(term), "toggled", GTK_SIGNAL_FUNC(term_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(box), term, FALSE, FALSE, 0);

    said = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(said), TRUE);
    gtk_label_set_justify(GTK_LABEL(said), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment(GTK_MISC(said), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(box), said, FALSE, FALSE, 0);

    row = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
    buttons = gtk_hbox_new(TRUE, 8);
    gtk_box_pack_end(GTK_BOX(row), buttons, FALSE, FALSE, 0);
    run = button(buttons, _("Run"), GTK_SIGNAL_FUNC(run_clicked));
    button(buttons, _("Close"), GTK_SIGNAL_FUNC(gtk_main_quit));
    more = button(buttons, "", GTK_SIGNAL_FUNC(more_clicked));

    list = gtk_clist_new(2);
    gtk_clist_set_selection_mode(GTK_CLIST(list), GTK_SELECTION_BROWSE);
    gtk_clist_set_column_width(GTK_CLIST(list), 0, NAMES);
    GTK_WIDGET_UNSET_FLAGS(list, GTK_CAN_FOCUS);
    gtk_signal_connect(GTK_OBJECT(list), "select_row", GTK_SIGNAL_FUNC(row_pressed), NULL);
    pane = scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
    gtk_widget_set_usize(scrolled, -1, HEIGHT);
    gtk_container_add(GTK_CONTAINER(scrolled), list);
    gtk_widget_show(list);
    gtk_box_pack_start(GTK_BOX(box), scrolled, TRUE, TRUE, 0);

    gtk_widget_show_all(box);
    gtk_widget_hide(said);
    gtk_widget_grab_default(run);
    gtk_widget_grab_focus(entry);
}

static void
usage(const gchar *argv0)
{
    printf(_("Usage: %s [OPTION...]\n"), argv0);
    printf("\n");
    printf(_("  --list       show the list of programs from the start\n"));
    printf(_("  --help       show this help and exit\n"));
    printf(_("  --version    show the version and exit\n"));
    printf("\n");
    printf(_("The usual GTK options, such as --display, are also understood.\n"));
}

int
main(int argc, char **argv)
{
    gboolean show = FALSE;
    GList *l;
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
            printf("cgrun %s\n", PACKAGE_VERSION);
            return 0;
        }

    gtk_init(&argc, &argv);
    for (i = 1; i < argc; i++)  /* what is left once GTK has taken its own */
        if (!strcmp(argv[i], "--list") || !strcmp(argv[i], "-l"))
            show = TRUE;
    alone();
    apps = apps_load();
    past_load();
    build();
    list_show(show);
    gtk_widget_show(win);
    gtk_main();

    apps_free(apps);
    for (l = cmds; l; l = l->next) {
        Cmd *cmd = l->data;

        g_free(cmd->line);
        g_free(cmd->key);
        g_free(cmd);
    }
    g_list_free(cmds);
    g_key_file_free(past);
    return 0;
}
