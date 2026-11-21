/* cgedit.c - a text editor: its window, its menus and its files

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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gdk/gdkkeysyms.h>

#include "cgedit.h"
#include "encoding.h"
#include "icons/cgedit.xpm"

enum { OTHERS = ENCODING_MAX_ITEM_NUM + 4 };  /* the most codesets the file selector offers */

Settings settings = { 600, 400, NULL, FALSE, FALSE, FALSE };

/* The file the text is of: its name, the codeset it is in, and how its lines end. */
static struct {
    gchar *name;     /* NULL until it has one */
    gchar *charset;  /* NULL to find out on opening */
    gint lineend;    /* LF, CR, or CR + LF */
} file = { NULL, NULL, LF };

static GtkWidget *win, *editor;
static GtkWidget *item_save, *item_undo, *item_redo, *item_cut, *item_copy, *item_delete;
static gchar *sought, *instead;  /* what was last looked for, and what it was to become */
static gboolean match_case, all_at_once, on_top;
static GdkPixmap *icon;
static GdkBitmap *icon_mask;

/* Something is said, and OK is all there is to answer. */
void
tell(GtkWindow *over, const gchar *text)
{
    GtkWidget *dialog = gtk_message_dialog_new(over, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", text);

    gtk_window_set_title(GTK_WINDOW(dialog), _("Text Editor"));
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static gboolean
exists(const gchar *name)
{
    return name && g_file_test(name, G_FILE_TEST_EXISTS);
}

static gboolean
writable(const gchar *name)
{
    return access(name, W_OK) == 0;
}

/*
 * The window is called after the file, as Leafpad called it: in
 * brackets if there is no such file yet, in angle brackets if it
 * cannot be written, and with a star before it while the text is not
 * as the file has it. What can be done is all that can be chosen.
 */
static void
state(void)
{
    static gchar *shown;
    gchar *base = file.name ? g_path_get_basename(file.name) : g_strdup(_("Untitled")), *title;
    gboolean modified = gtk_editor_get_modified(GTK_EDITOR(editor));
    gboolean sel = gtk_editor_get_selection(GTK_EDITOR(editor), NULL, NULL);
    const gchar *open = "", *shut = "";

    if (!exists(file.name)) {
        open = "(";
        shut = ")";
    } else if (!writable(file.name)) {
        open = "<";
        shut = ">";
    }
    title = g_strconcat(modified ? "*" : "", open, base, shut, NULL);
    if (!shown || strcmp(title, shown)) {  /* this is come to at every key, and the title is seldom another */
        gtk_window_set_title(GTK_WINDOW(win), title);
        g_free(shown);
        shown = g_strdup(title);
    }
    g_free(title);
    g_free(base);
    gtk_widget_set_sensitive(item_save, modified || !exists(file.name));
    gtk_widget_set_sensitive(item_undo, gtk_editor_can_undo(GTK_EDITOR(editor)));
    gtk_widget_set_sensitive(item_redo, gtk_editor_can_redo(GTK_EDITOR(editor)));
    gtk_widget_set_sensitive(item_cut, sel);
    gtk_widget_set_sensitive(item_copy, sel);
    gtk_widget_set_sensitive(item_delete, sel);
}

/* The star and the menus follow the text; what can be chosen of a selection follows that. */
static void
changed(GtkWidget *widget G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
    state();
}

static void
moved(GtkWidget *widget G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
    gboolean sel = gtk_editor_get_selection(GTK_EDITOR(editor), NULL, NULL);

    gtk_widget_set_sensitive(item_cut, sel);
    gtk_widget_set_sensitive(item_copy, sel);
    gtk_widget_set_sensitive(item_delete, sel);
}

/* Files. */

/*
 * A file is read into the editor. How its lines end is found and
 * kept for saving; its codeset is the one asked for or else found out
 * as Leafpad found it, and if the text cannot be read as that, it is
 * read as Latin-1, which anything can be. A file that starts with
 * the mark of UTF-16 is that. A name with no file is a new file.
 */
static gboolean
load(const gchar *name, const gchar *charset)
{
    GError *error = NULL;
    gchar *data, *text = NULL, *wide = NULL;
    const gchar *found = charset;
    gsize len, n;

    if (!g_file_get_contents(name, &data, &len, &error)) {
        if (exists(name)) {
            tell(GTK_WINDOW(win), error->message);
            g_error_free(error);
            return FALSE;
        }
        g_error_free(error);
        data = g_strdup("");
        len = 0;
    }
    if (!charset && len >= 2 && ((guchar) data[0] ^ (guchar) data[1]) == 1 && (guchar) data[0] >= 0xfe
        && (wide = g_convert(data, len, "UTF-8", "UTF-16", NULL, &n, NULL))) {
        g_free(data);
        data = wide;
        len = n;
        found = "UTF-16";
        charset = "UTF-8";  /* which it is by now */
    }
    file.lineend = detect_line_ending(data);
    if (file.lineend != LF && strlen(data) == len) {
        convert_line_ending_to_lf(data);
        len = strlen(data);
    }
    if (!charset && !(charset = found = detect_charset(data)))
        charset = found = get_default_charset();
    if (len)
        while (!(text = g_convert(data, len, "UTF-8", charset, NULL, &n, NULL))) {
            if (!strcmp(charset, "ISO-8859-1")) {  /* which cannot be, but it must end */
                text = g_strdup(data);
                n = len;
                break;
            }
            charset = found = "ISO-8859-1";
        }
    else
        n = 0;
    gtk_editor_set_text(GTK_EDITOR(editor), text ? text : "", n);
    g_free(text);
    g_free(data);

    if (name != file.name) {
        g_free(file.name);
        file.name = g_strdup(name);
    }
    if (found != file.charset) {
        g_free(file.charset);
        file.charset = g_strdup(found);
    }
    state();
    return TRUE;
}

/* The text goes to its file, in the file's codeset and with its line ends. */
static gboolean
store(void)
{
    gchar *text = gtk_editor_get_text(GTK_EDITOR(editor), 0, G_MAXINT), *out, *said;
    GError *error = NULL;
    gboolean wrote;
    gsize n;
    FILE *fp;

    if (file.lineend != LF)
        convert_line_ending(&text, file.lineend);
    if (!file.charset)
        file.charset = g_strdup(get_default_charset());
    out = g_convert(text, -1, file.charset, "UTF-8", NULL, &n, &error);
    g_free(text);
    if (!out) {
        if (error->code == G_CONVERT_ERROR_ILLEGAL_SEQUENCE) {
            said = g_strdup_printf(_("Can't convert codeset to '%s'"), file.charset);
            tell(GTK_WINDOW(win), said);
            g_free(said);
        } else
            tell(GTK_WINDOW(win), error->message);
        g_error_free(error);
        return FALSE;
    }
    /* Written over the file as it stands, so that it keeps its owner, its mode and its links. */
    fp = fopen(file.name, "w");
    wrote = fp && fwrite(out, 1, n, fp) == n;
    if (fp && fclose(fp) != 0)
        wrote = FALSE;
    if (!wrote) {
        said = g_strdup_printf(_("Can't open file to write: %s"), g_strerror(errno));
        tell(GTK_WINDOW(win), said);
        g_free(said);
        g_free(out);
        return FALSE;
    }
    g_free(out);
    gtk_editor_set_saved(GTK_EDITOR(editor));
    state();
    return TRUE;
}

/* What the file selector is asked besides the file. */
typedef struct {
    const gchar *codes[OTHERS];  /* NULL to find out, which only opening can */
    gint n, chosen, other;       /* how many, which is chosen, and which is the one typed in */
    gchar *typed;
    GtkWidget *menu, *other_text;  /* the label of the last, which the menu moves about */
    gint lineend;
} Asked;

static void
other_label(Asked *asked)
{
    gchar *label = asked->typed ? g_strdup_printf(_("Other Codeset (%s)"), asked->typed)
        : g_strdup(_("Other Codeset..."));

    gtk_label_set_text(GTK_LABEL(asked->other_text), label);
    g_free(label);
}

/* A codeset the list has not is typed in, and taken if text can be turned into it. */
static void
other_codeset(Asked *asked, gint was)
{
    GtkWidget *dialog = gtk_dialog_new(), *box = gtk_hbox_new(FALSE, 8), *entry = gtk_entry_new();
    gchar *said, *try;

    gtk_window_set_title(GTK_WINDOW(dialog), _("Other Codeset"));
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(gtk_widget_get_toplevel(asked->menu)));
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new(_("Codeset:")), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), box, TRUE, TRUE, 0);
    if (asked->typed)
        gtk_entry_set_text(GTK_ENTRY(entry), asked->typed);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("OK"), GTK_RESPONSE_OK);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_signal_connect_object(GTK_OBJECT(entry), "activate", GTK_SIGNAL_FUNC(gtk_window_activate_default),
                              GTK_OBJECT(dialog));
    gtk_widget_show_all(box);
    gtk_widget_grab_focus(entry);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK && *gtk_entry_get_text(GTK_ENTRY(entry))) {
        if ((try = g_convert("TEST", -1, "UTF-8", gtk_entry_get_text(GTK_ENTRY(entry)), NULL, NULL, NULL))) {
            g_free(try);
            g_free(asked->typed);
            asked->typed = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
            other_label(asked);
            was = asked->other;
        } else {
            said = g_strdup_printf(_("'%s' is not supported"), gtk_entry_get_text(GTK_ENTRY(entry)));
            tell(GTK_WINDOW(dialog), said);
            g_free(said);
        }
    }
    gtk_widget_destroy(dialog);
    asked->chosen = was;
    gtk_option_menu_set_history(GTK_OPTION_MENU(asked->menu), was);
}

static void
codeset_chosen(GtkWidget *item, Asked *asked)
{
    gint which = GPOINTER_TO_INT(gtk_object_get_user_data(GTK_OBJECT(item)));

    if (which == asked->other)
        other_codeset(asked, asked->typed ? asked->other : asked->chosen);
    else
        asked->chosen = which;
}

static void
lineend_chosen(GtkWidget *item, Asked *asked)
{
    asked->lineend = GPOINTER_TO_INT(gtk_object_get_user_data(GTK_OBJECT(item)));
}

static GtkWidget *
option(GtkWidget *menu, const gchar *label, gint which, GtkSignalFunc chosen, Asked *asked)
{
    GtkWidget *item = gtk_menu_item_new_with_label(label);

    gtk_object_set_user_data(GTK_OBJECT(item), GINT_TO_POINTER(which));
    gtk_signal_connect(GTK_OBJECT(item), "activate", chosen, asked);
    gtk_menu_append(GTK_MENU(menu), item);
    gtk_widget_show(item);
    return item;
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
 * One of the toolkit's windows with an OK and a Cancel of its own is
 * made to wait for one of them: each time gtk_main() then returns,
 * *answer says which it was.
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

/*
 * A file is chosen, to open or to save as, and with it its codeset:
 * to be found out, when opening; the locale's; UTF-8; those that the
 * locale's language has had; or one typed in. Saving asks how lines
 * end as well. The name, to be freed, or NULL; the rest is put in
 * file if a name was given.
 */
static gchar *
choose_file(gboolean saving)
{
    GtkWidget *fs = gtk_file_selection_new(saving ? _("Save As") : _("Open")), *row, *menu, *ends;
    EncArray *known = get_encoding_items(get_encoding_code());
    const gchar *locale = get_default_charset();
    gchar *name = NULL, *label;
    Asked asked;
    gint i, answer = FALSE;

    memset(&asked, 0, sizeof asked);
    asked.lineend = file.lineend;
    row = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(row), gtk_label_new(_("Character Coding:")), FALSE, FALSE, 0);
    asked.menu = gtk_option_menu_new();
    menu = gtk_menu_new();
    if (!saving) {
        option(menu, _("Auto-Detect"), asked.n, GTK_SIGNAL_FUNC(codeset_chosen), &asked);
        asked.codes[asked.n++] = NULL;
    }
    label = g_strdup_printf(_("Current Locale (%s)"), locale);
    option(menu, label, asked.n, GTK_SIGNAL_FUNC(codeset_chosen), &asked);
    g_free(label);
    asked.codes[asked.n++] = locale;
    if (g_strcasecmp(locale, "UTF-8")) {
        option(menu, "UTF-8", asked.n, GTK_SIGNAL_FUNC(codeset_chosen), &asked);
        asked.codes[asked.n++] = "UTF-8";
    }
    for (i = 0; i < ENCODING_MAX_ITEM_NUM; i++)
        if (known->item[i] && g_strcasecmp(known->item[i], locale)) {
            option(menu, known->item[i], asked.n, GTK_SIGNAL_FUNC(codeset_chosen), &asked);
            asked.codes[asked.n++] = known->item[i];
        }
    asked.other = asked.n;
    asked.other_text = GTK_BIN(option(menu, "", asked.n, GTK_SIGNAL_FUNC(codeset_chosen), &asked))->child;
    asked.codes[asked.n++] = NULL;
    /* Saving starts from the codeset the file is in; if the list has it not, it is the one typed in. */
    if (saving && file.charset) {
        for (i = 0; i < asked.other && g_strcasecmp(asked.codes[i], file.charset); i++)
            ;
        asked.chosen = i;
        if (i == asked.other)
            asked.typed = g_strdup(file.charset);
    }
    other_label(&asked);
    gtk_option_menu_set_menu(GTK_OPTION_MENU(asked.menu), menu);
    gtk_option_menu_set_history(GTK_OPTION_MENU(asked.menu), asked.chosen);
    gtk_box_pack_start(GTK_BOX(row), asked.menu, FALSE, FALSE, 0);
    if (saving) {
        ends = gtk_option_menu_new();
        menu = gtk_menu_new();
        option(menu, "LF", LF, GTK_SIGNAL_FUNC(lineend_chosen), &asked);
        option(menu, "CR+LF", CR + LF, GTK_SIGNAL_FUNC(lineend_chosen), &asked);
        option(menu, "CR", CR, GTK_SIGNAL_FUNC(lineend_chosen), &asked);
        gtk_option_menu_set_menu(GTK_OPTION_MENU(ends), menu);
        gtk_option_menu_set_history(GTK_OPTION_MENU(ends), file.lineend == CR + LF ? 1 : file.lineend == CR ? 2 : 0);
        gtk_box_pack_start(GTK_BOX(row), ends, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(GTK_FILE_SELECTION(fs)->action_area), row, FALSE, FALSE, 0);
    gtk_widget_show_all(row);

    /* Saving starts from the file's name, and opening from where the file is. */
    if (file.name && saving)
        gtk_file_selection_set_filename(GTK_FILE_SELECTION(fs), file.name);
    else if (file.name) {
        name = g_path_get_dirname(file.name);
        label = g_strconcat(name, "/", NULL);
        gtk_file_selection_set_filename(GTK_FILE_SELECTION(fs), label);
        g_free(label);
        g_free(name);
        name = NULL;
    }
    waits(fs, GTK_FILE_SELECTION(fs)->ok_button, GTK_FILE_SELECTION(fs)->cancel_button, &answer);
    for (;;) {
        gtk_main();
        if (!answer)
            break;
        name = g_strdup(gtk_file_selection_get_filename(GTK_FILE_SELECTION(fs)));
        if (!g_file_test(name, G_FILE_TEST_IS_DIR))
            break;
        /* A directory was named: that is where to look, not what to open. */
        label = g_strconcat(name, name[strlen(name) - 1] == '/' ? "" : "/", NULL);
        gtk_file_selection_set_filename(GTK_FILE_SELECTION(fs), label);
        g_free(label);
        g_free(name);
        name = NULL;
    }
    gtk_widget_destroy(fs);
    if (name && *name) {
        g_free(file.charset);
        file.charset = g_strdup(asked.chosen == asked.other ? asked.typed : asked.codes[asked.chosen]);
        file.lineend = asked.lineend;
    } else {
        g_free(name);
        name = NULL;
    }
    g_free(asked.typed);
    return name;
}

static gboolean
save(gboolean as)
{
    gchar *name, *was = file.name, *said;
    GtkWidget *dialog;
    gint answer;

    if (as || !file.name) {
        if (!(name = choose_file(TRUE)))
            return FALSE;
        if (exists(name) && (!was || strcmp(name, was))) {
            said = g_strdup_printf(_("'%s' already exists. Overwrite?"), name);
            dialog = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                            GTK_BUTTONS_YES_NO, "%s", said);
            g_free(said);
            gtk_window_set_title(GTK_WINDOW(dialog), _("Save As"));
            answer = gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            if (answer != GTK_RESPONSE_YES) {
                g_free(name);
                return FALSE;
            }
        }
        file.name = name;
        if (!store()) {
            g_free(name);
            file.name = was;
            state();
            return FALSE;
        }
        g_free(was);
        return TRUE;
    }
    return store();
}

/* Before the text goes: is it to be saved? FALSE if it is to stay after all. */
static gboolean
may_go(void)
{
    GtkWidget *dialog;
    gchar *base;
    gint answer;

    if (!gtk_editor_get_modified(GTK_EDITOR(editor)))
        return TRUE;
    base = file.name ? g_path_get_basename(file.name) : g_strdup(_("Untitled"));
    dialog = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                    GTK_BUTTONS_NONE, _("Save changes to '%s'?"), base);
    g_free(base);
    gtk_window_set_title(GTK_WINDOW(dialog), _("Text Editor"));
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("No"), GTK_RESPONSE_NO);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("Yes"), GTK_RESPONSE_YES);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_YES);
    answer = gtk_dialog_run(GTK_DIALOG(dialog));
    if (answer != GTK_RESPONSE_NONE)
        gtk_widget_destroy(dialog);
    return answer == GTK_RESPONSE_YES ? save(FALSE) : answer == GTK_RESPONSE_NO;
}

/* The settings. */

static gchar *
settings_file(void)
{
    return g_build_filename(g_get_user_config_dir(), "cgedit", "cgeditrc", NULL);
}

static void
settings_load(void)
{
    GKeyFile *kept = g_key_file_new();
    gchar *name = settings_file();
    gint n;

    if (g_key_file_load_from_file(kept, name, G_KEY_FILE_NONE, NULL)) {
        if ((n = g_key_file_get_integer(kept, "cgedit", "Width", NULL)) > 0)
            settings.width = n;
        if ((n = g_key_file_get_integer(kept, "cgedit", "Height", NULL)) > 0)
            settings.height = n;
        settings.font = g_key_file_get_string(kept, "cgedit", "Font", NULL);
        settings.wrap = g_key_file_get_boolean(kept, "cgedit", "WordWrap", NULL);
        settings.numbers = g_key_file_get_boolean(kept, "cgedit", "LineNumbers", NULL);
        settings.indent = g_key_file_get_boolean(kept, "cgedit", "AutoIndent", NULL);
    }
    g_key_file_free(kept);
    g_free(name);
}

static void
settings_save(void)
{
    GKeyFile *kept = g_key_file_new();
    gchar *name = settings_file(), *dir = g_path_get_dirname(name);

    g_key_file_set_integer(kept, "cgedit", "Width", win->allocation.width);
    g_key_file_set_integer(kept, "cgedit", "Height", win->allocation.height);
    if (settings.font)
        g_key_file_set_string(kept, "cgedit", "Font", settings.font);
    g_key_file_set_boolean(kept, "cgedit", "WordWrap", settings.wrap);
    g_key_file_set_boolean(kept, "cgedit", "LineNumbers", settings.numbers);
    g_key_file_set_boolean(kept, "cgedit", "AutoIndent", settings.indent);
    if (g_mkdir_with_parents(dir, 0700) == 0)
        g_key_file_save_to_file(kept, name, NULL);
    g_key_file_free(kept);
    g_free(dir);
    g_free(name);
}

static void
leave(void)
{
    if (!may_go())
        return;
    settings_save();
    gtk_main_quit();
}

/* The menus. */

enum { FILE_NEW, FILE_OPEN, FILE_SAVE, FILE_SAVE_AS, FILE_QUIT };

static void
file_menu(gpointer data G_GNUC_UNUSED, guint action, GtkWidget *item G_GNUC_UNUSED)
{
    gchar *name, *was;

    switch (action) {
    case FILE_NEW:
        if (!may_go())
            break;
        g_free(file.name);
        g_free(file.charset);
        file.name = file.charset = NULL;
        file.lineend = LF;
        gtk_editor_set_text(GTK_EDITOR(editor), "", 0);
        state();
        break;
    case FILE_OPEN:
        if (!may_go())
            break;
        was = g_strdup(file.charset);
        if ((name = choose_file(FALSE))) {
            load(name, file.charset);
            g_free(name);
        } else {
            g_free(file.charset);
            file.charset = g_strdup(was);
        }
        g_free(was);
        break;
    case FILE_SAVE:
    case FILE_SAVE_AS:
        save(action == FILE_SAVE_AS);
        break;
    case FILE_QUIT:
        leave();
        break;
    }
}

enum { EDIT_UNDO, EDIT_REDO, EDIT_CUT, EDIT_COPY, EDIT_PASTE, EDIT_DELETE, EDIT_ALL };

static void
edit_menu(gpointer data G_GNUC_UNUSED, guint action, GtkWidget *item G_GNUC_UNUSED)
{
    GtkEditor *ed = GTK_EDITOR(editor);

    switch (action) {
    case EDIT_UNDO: gtk_editor_undo(ed); break;
    case EDIT_REDO: gtk_editor_redo(ed); break;
    case EDIT_CUT: gtk_editor_cut(ed); break;
    case EDIT_COPY: gtk_editor_copy(ed); break;
    case EDIT_PASTE: gtk_editor_paste(ed); break;
    case EDIT_DELETE: gtk_editor_delete_selection(ed); break;
    case EDIT_ALL: gtk_editor_select_all(ed); break;
    }
}

/* A dialog of this program's own: two buttons, and room above them. */
static GtkWidget *
dialog_new(const gchar *title, const gchar *ok, GtkWidget **box)
{
    GtkWidget *dialog = gtk_dialog_new();

    gtk_window_set_title(GTK_WINDOW(dialog), title);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(win));
    gtk_window_set_policy(GTK_WINDOW(dialog), FALSE, FALSE, TRUE);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), ok, GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    *box = gtk_table_new(4, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(*box), 8);
    gtk_table_set_col_spacings(GTK_TABLE(*box), 8);
    gtk_container_set_border_width(GTK_CONTAINER(*box), 10);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), *box, TRUE, TRUE, 0);
    return dialog;
}

/* A line to type in, with what it is before it; Return in it is the dialog's OK. */
static GtkWidget *
line(GtkWidget *dialog, GtkWidget *table, gint row, const gchar *label, const gchar *text)
{
    GtkWidget *l = gtk_label_new(label), *entry = gtk_entry_new();

    gtk_misc_set_alignment(GTK_MISC(l), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), l, 0, 1, row, row + 1, GTK_FILL, 0, 0, 0);
    gtk_widget_set_usize(entry, 220, -1);
    if (text)
        gtk_entry_set_text(GTK_ENTRY(entry), text);
    gtk_signal_connect_object(GTK_OBJECT(entry), "activate", GTK_SIGNAL_FUNC(gtk_window_activate_default),
                              GTK_OBJECT(dialog));
    gtk_table_attach_defaults(GTK_TABLE(table), entry, 1, 2, row, row + 1);
    return entry;
}

static GtkWidget *
tick(GtkWidget *table, gint row, const gchar *label, gboolean on)
{
    GtkWidget *check = gtk_check_button_new_with_label(label);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), on);
    gtk_table_attach_defaults(GTK_TABLE(table), check, 0, 2, row, row + 1);
    return check;
}

/* What is looked for is found, onwards or back, and round the ends; or it is said that it is not there. */
static gboolean
find(gboolean forward)
{
    if (gtk_editor_find(GTK_EDITOR(editor), sought, forward, match_case))
        return TRUE;
    tell(GTK_WINDOW(win), _("Search string not found"));
    return FALSE;
}

/*
 * Every place is gone to in turn, from the cursor on to the end of
 * the text, and asked about: Yes makes it what it is to become, No
 * leaves it, Cancel stops.
 */
static void
replace_each(void)
{
    GtkEditor *ed = GTK_EDITOR(editor);
    GtkWidget *dialog;
    gsize start, end, from = gtk_editor_get_cursor(ed);
    gboolean any = FALSE;
    gint answer;

    gtk_editor_get_selection(ed, &from, NULL);
    gtk_editor_select(ed, from, from);
    while (gtk_editor_find(ed, sought, TRUE, match_case)) {
        gtk_editor_get_selection(ed, &start, &end);
        if (start < from)  /* round the end and back at the top: all have been asked about */
            break;
        any = TRUE;
        gtk_editor_set_highlight(ed, sought, match_case);
        dialog = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                        GTK_BUTTONS_NONE, "%s", _("Replace?"));
        gtk_window_set_title(GTK_WINDOW(dialog), _("Replace"));
        gtk_dialog_add_button(GTK_DIALOG(dialog), _("Yes"), GTK_RESPONSE_YES);
        gtk_dialog_add_button(GTK_DIALOG(dialog), _("No"), GTK_RESPONSE_NO);
        gtk_dialog_add_button(GTK_DIALOG(dialog), _("Cancel"), GTK_RESPONSE_CANCEL);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_YES);
        answer = gtk_dialog_run(GTK_DIALOG(dialog));
        if (answer != GTK_RESPONSE_NONE)
            gtk_widget_destroy(dialog);
        if (answer == GTK_RESPONSE_YES) {
            gtk_editor_insert(ed, instead, -1);
            from = gtk_editor_get_cursor(ed);
        } else if (answer == GTK_RESPONSE_NO)
            from = end;
        else
            break;
        gtk_editor_select(ed, from, from);
    }
    gtk_editor_set_highlight(ed, NULL, FALSE);
    if (!any)
        tell(GTK_WINDOW(win), _("Search string not found"));
}

enum { SEARCH_FIND, SEARCH_NEXT, SEARCH_PREVIOUS, SEARCH_REPLACE, SEARCH_JUMP };

static void
search_menu(gpointer data G_GNUC_UNUSED, guint action, GtkWidget *item G_GNUC_UNUSED)
{
    GtkEditor *ed = GTK_EDITOR(editor);
    GtkWidget *dialog, *table, *what, *with = NULL, *check, *all = NULL, *spin;
    GtkObject *adj;
    gchar *said, *picked = NULL;
    gsize start, end;
    gint n;

    switch (action) {
    case SEARCH_NEXT:
    case SEARCH_PREVIOUS:
        if (sought) {
            gtk_editor_set_highlight(ed, sought, match_case);
            find(action == SEARCH_NEXT);
            break;
        }
        action = SEARCH_FIND;  /* nothing has been looked for yet */
        /* fall through */
    case SEARCH_FIND:
    case SEARCH_REPLACE:
        dialog = dialog_new(action == SEARCH_FIND ? _("Find") : _("Replace"),
                            action == SEARCH_FIND ? _("Find") : _("Find and Replace"), &table);
        /* What is selected on one line is what is looked for, to begin with. */
        if (gtk_editor_get_selection(ed, &start, &end) && gtk_editor_line_of(ed, start) == gtk_editor_line_of(ed, end))
            picked = gtk_editor_get_text(ed, start, end);
        what = line(dialog, table, 0, _("Find what:"), picked ? picked : sought);
        g_free(picked);
        if (action == SEARCH_REPLACE)
            with = line(dialog, table, 1, _("Replace with:"), instead);
        check = tick(table, 2, _("Match case"), match_case);
        if (action == SEARCH_REPLACE)
            all = tick(table, 3, _("Replace all at once"), all_at_once);
        gtk_widget_show_all(table);
        gtk_widget_grab_focus(what);
        gtk_editable_select_region(GTK_EDITABLE(what), 0, -1);
        n = gtk_dialog_run(GTK_DIALOG(dialog));
        if (n == GTK_RESPONSE_OK && *gtk_entry_get_text(GTK_ENTRY(what))) {
            g_free(sought);
            sought = g_strdup(gtk_entry_get_text(GTK_ENTRY(what)));
            match_case = GTK_TOGGLE_BUTTON(check)->active;
            if (with) {
                g_free(instead);
                instead = g_strdup(gtk_entry_get_text(GTK_ENTRY(with)));
                all_at_once = GTK_TOGGLE_BUTTON(all)->active;
            }
        } else
            n = GTK_RESPONSE_CANCEL;
        if (n != GTK_RESPONSE_NONE)
            gtk_widget_destroy(dialog);
        if (n != GTK_RESPONSE_OK)
            break;
        if (action == SEARCH_FIND) {
            gtk_editor_set_highlight(ed, sought, match_case);
            find(TRUE);
        } else if (!all_at_once)
            replace_each();
        else if ((n = gtk_editor_replace_all(ed, sought, instead, match_case))) {
            gtk_editor_set_highlight(ed, instead, TRUE);
            said = g_strdup_printf(_("%d strings replaced"), n);
            dialog = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
                                            GTK_BUTTONS_OK, "%s", said);
            g_free(said);
            gtk_window_set_title(GTK_WINDOW(dialog), _("Replace"));
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
        } else
            tell(GTK_WINDOW(win), _("Search string not found"));
        break;
    case SEARCH_JUMP:
        dialog = dialog_new(_("Jump To"), _("Jump"), &table);
        n = gtk_editor_line_of(ed, gtk_editor_get_cursor(ed)) + 1;
        adj = gtk_adjustment_new(n, 1, gtk_editor_get_lines(ed), 1, 10, 0);
        spin = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 1, 0);
        gtk_table_attach(GTK_TABLE(table), gtk_label_new(_("Line number:")), 0, 1, 0, 1, GTK_FILL, 0, 0, 0);
        gtk_table_attach_defaults(GTK_TABLE(table), spin, 1, 2, 0, 1);
        gtk_signal_connect_object(GTK_OBJECT(spin), "activate", GTK_SIGNAL_FUNC(gtk_window_activate_default),
                                  GTK_OBJECT(dialog));
        gtk_widget_show_all(table);
        gtk_widget_grab_focus(spin);
        gtk_editable_select_region(GTK_EDITABLE(spin), 0, -1);
        n = gtk_dialog_run(GTK_DIALOG(dialog));
        if (n == GTK_RESPONSE_OK) {
            gtk_spin_button_update(GTK_SPIN_BUTTON(spin));
            start = gtk_editor_line_start(ed, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin)) - 1);
            gtk_editor_select(ed, start, start);
        }
        if (n != GTK_RESPONSE_NONE)
            gtk_widget_destroy(dialog);
        break;
    }
}

/* The font the settings name is the editor's; no name, or none of that name, is the theme's. */
static void
set_font(void)
{
    GdkFont *font = settings.font ? gdk_font_load(settings.font) : NULL;

    gtk_editor_set_font(GTK_EDITOR(editor), font);
    if (font)
        gdk_font_unref(font);
}

enum { OPTION_FONT, OPTION_WRAP, OPTION_NUMBERS, OPTION_INDENT };

static void
option_menu(gpointer data G_GNUC_UNUSED, guint action, GtkWidget *item)
{
    GtkFontSelectionDialog *fsd;
    GtkWidget *dialog;
    GtkEditor *ed;
    gint answer = FALSE;

    if (!editor)  /* the menus are ticked as the settings have them before there is an editor */
        return;
    ed = GTK_EDITOR(editor);
    switch (action) {
    case OPTION_FONT:
        dialog = gtk_font_selection_dialog_new(_("Font"));
        fsd = GTK_FONT_SELECTION_DIALOG(dialog);
        if (settings.font)
            gtk_font_selection_dialog_set_font_name(fsd, settings.font);
        gtk_widget_hide(fsd->apply_button);
        waits(dialog, fsd->ok_button, fsd->cancel_button, &answer);
        gtk_main();
        if (answer && gtk_font_selection_dialog_get_font(fsd)) {
            g_free(settings.font);
            settings.font = gtk_font_selection_dialog_get_font_name(fsd);
            set_font();
        }
        gtk_widget_destroy(dialog);
        break;
    case OPTION_WRAP:
        settings.wrap = GTK_CHECK_MENU_ITEM(item)->active;
        gtk_editor_set_wrap(ed, settings.wrap);
        break;
    case OPTION_NUMBERS:
        settings.numbers = GTK_CHECK_MENU_ITEM(item)->active;
        gtk_editor_set_line_numbers(ed, settings.numbers);
        break;
    case OPTION_INDENT:
        settings.indent = GTK_CHECK_MENU_ITEM(item)->active;
        gtk_editor_set_auto_indent(ed, settings.indent);
        break;
    }
}

static void
about(gpointer data G_GNUC_UNUSED, guint action G_GNUC_UNUSED, GtkWidget *item G_GNUC_UNUSED)
{
    GtkWidget *dialog = gtk_dialog_new(), *box = gtk_vbox_new(FALSE, 4);
    gchar *text = g_strdup_printf("CGEdit %s", PACKAGE_VERSION);

    gtk_window_set_title(GTK_WINDOW(dialog), _("About CGEdit"));
    gtk_window_set_policy(GTK_WINDOW(dialog), FALSE, FALSE, TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(win));
    gtk_container_set_border_width(GTK_CONTAINER(box), 16);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), box, TRUE, TRUE, 0);
    if (icon)
        gtk_box_pack_start(GTK_BOX(box), gtk_pixmap_new(icon, icon_mask), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new(text), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new(_("A simple text editor, as Leafpad is")), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new("(C) 2026 The CGTK authors"), FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new(_("Codeset detection from Leafpad:")), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new("(C) 2004-2005 Tarot Osuji"), FALSE, FALSE, 0);
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
    { "/File", N_("_New"), "<control>N", file_menu, FILE_NEW, NULL },
    { "/File", N_("_Open..."), "<control>O", file_menu, FILE_OPEN, NULL },
    { "/File", N_("_Save"), "<control>S", file_menu, FILE_SAVE, NULL },
    { "/File", N_("Save _As..."), "<shift><control>S", file_menu, FILE_SAVE_AS, NULL },
    { "/File", "-", NULL, NULL, 0, "<Separator>" },
    { "/File", N_("_Quit"), "<control>Q", file_menu, FILE_QUIT, NULL },
    { "", N_("_Edit"), NULL, NULL, 0, "<Branch>" },
    { "/Edit", N_("_Undo"), "<control>Z", edit_menu, EDIT_UNDO, NULL },
    { "/Edit", N_("_Redo"), "<shift><control>Z", edit_menu, EDIT_REDO, NULL },
    { "/Edit", "-", NULL, NULL, 0, "<Separator>" },
    { "/Edit", N_("Cu_t"), "<control>X", edit_menu, EDIT_CUT, NULL },
    { "/Edit", N_("_Copy"), "<control>C", edit_menu, EDIT_COPY, NULL },
    { "/Edit", N_("_Paste"), "<control>V", edit_menu, EDIT_PASTE, NULL },
    { "/Edit", N_("_Delete"), NULL, edit_menu, EDIT_DELETE, NULL },
    { "/Edit", "--", NULL, NULL, 0, "<Separator>" },
    { "/Edit", N_("Select _All"), "<control>A", edit_menu, EDIT_ALL, NULL },
    { "", N_("_Search"), NULL, NULL, 0, "<Branch>" },
    { "/Search", N_("_Find..."), "<control>F", search_menu, SEARCH_FIND, NULL },
    { "/Search", N_("Find _Next"), "<control>G", search_menu, SEARCH_NEXT, NULL },
    { "/Search", N_("Find _Previous"), "<shift><control>G", search_menu, SEARCH_PREVIOUS, NULL },
    { "/Search", N_("_Replace..."), "<control>H", search_menu, SEARCH_REPLACE, NULL },
    { "/Search", "-", NULL, NULL, 0, "<Separator>" },
    { "/Search", N_("_Jump To..."), "<control>J", search_menu, SEARCH_JUMP, NULL },
    { "", N_("_Options"), NULL, NULL, 0, "<Branch>" },
    { "/Options", N_("_Font..."), NULL, option_menu, OPTION_FONT, NULL },
    { "/Options", N_("_Word Wrap"), NULL, option_menu, OPTION_WRAP, "<CheckItem>" },
    { "/Options", N_("_Line Numbers"), NULL, option_menu, OPTION_NUMBERS, "<CheckItem>" },
    { "/Options", "-", NULL, NULL, 0, "<Separator>" },
    { "/Options", N_("_Auto Indent"), NULL, option_menu, OPTION_INDENT, "<CheckItem>" },
    { "", N_("_Help"), NULL, NULL, 0, "<Branch>" },
    { "/Help", N_("_About"), NULL, about, 0, NULL }
};

/* Only the last part of a path is a label. */
static gchar *
menu_translate(const gchar *path, gpointer data G_GNUC_UNUSED)
{
    const gchar *label = strrchr(path, '/');

    return _(label ? label + 1 : path);
}

static void
ticked(GtkItemFactory *factory, const gchar *path, gboolean on)
{
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(gtk_item_factory_get_widget(factory, path)), on);
}

static GtkWidget *
menus(GtkAccelGroup *accel)
{
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
    item_save = gtk_item_factory_get_widget(factory, "/File/Save");
    item_undo = gtk_item_factory_get_widget(factory, "/Edit/Undo");
    item_redo = gtk_item_factory_get_widget(factory, "/Edit/Redo");
    item_cut = gtk_item_factory_get_widget(factory, "/Edit/Cut");
    item_copy = gtk_item_factory_get_widget(factory, "/Edit/Copy");
    item_delete = gtk_item_factory_get_widget(factory, "/Edit/Delete");
    ticked(factory, "/Options/Word Wrap", settings.wrap);
    ticked(factory, "/Options/Line Numbers", settings.numbers);
    ticked(factory, "/Options/Auto Indent", settings.indent);
    return gtk_item_factory_get_widget(factory, "<main>");
}

/*
 * Two keys are this program's and no menu's, as they were Leafpad's:
 * Control+Tab makes a tab four spaces wide and then eight again, and
 * Control+T keeps the window above the others or lets it be. The
 * editor hears of a key only after this.
 */
static gint
key_press(GtkWidget *widget, GdkEventKey *event, gpointer data G_GNUC_UNUSED)
{
    static gint usual;

    if (!(event->state & GDK_CONTROL_MASK))
        return FALSE;
    if (event->keyval == GDK_Tab || event->keyval == GDK_ISO_Left_Tab) {
        if (!usual)
            usual = gtk_editor_get_tab_width(GTK_EDITOR(widget));
        gtk_editor_set_tab_width(GTK_EDITOR(widget), gtk_editor_get_tab_width(GTK_EDITOR(widget)) != usual
                                 ? usual : usual == 8 ? 4 : 8);
    } else if (gdk_keyval_to_lower(event->keyval) == GDK_t && !(event->state & GDK_SHIFT_MASK)) {
        on_top = !on_top;
        gdk_window_set_keep_above(win->window, on_top);
    } else
        return FALSE;
    gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event");
    return TRUE;
}

/* A file dropped on the text is opened: the first, if there are several. */
static void
dropped(GtkWidget *widget G_GNUC_UNUSED, const gchar *list, gpointer data G_GNUC_UNUSED)
{
    gchar **uris = g_strsplit(list, "\n", 2);
    gchar *name = uris[0] ? g_filename_from_uri(g_strchomp(uris[0]), NULL, NULL) : NULL;

    g_strfreev(uris);
    if (name && may_go()) {
        g_free(file.charset);
        file.charset = NULL;
        load(name, NULL);
    }
    g_free(name);
}

static gint
delete_event(GtkWidget *widget G_GNUC_UNUSED, GdkEvent *event G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
    leave();
    return TRUE;
}

/* The window, as Leafpad's was: the menus, and the text. */
static void
build(void)
{
    GtkAccelGroup *accel = gtk_accel_group_new();
    GtkWidget *box, *scrolled;

    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_wmclass(GTK_WINDOW(win), "cgedit", "Cgedit");
    gtk_window_set_default_size(GTK_WINDOW(win), settings.width, settings.height);
    gtk_window_add_accel_group(GTK_WINDOW(win), accel);
    gtk_signal_connect(GTK_OBJECT(win), "delete_event", GTK_SIGNAL_FUNC(delete_event), NULL);

    box = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(win), box);
    gtk_box_pack_start(GTK_BOX(box), menus(accel), FALSE, FALSE, 0);

    editor = gtk_editor_new();
    gtk_editor_set_wrap(GTK_EDITOR(editor), settings.wrap);
    gtk_editor_set_line_numbers(GTK_EDITOR(editor), settings.numbers);
    gtk_editor_set_auto_indent(GTK_EDITOR(editor), settings.indent);
    set_font();
    gtk_signal_connect(GTK_OBJECT(editor), "changed", GTK_SIGNAL_FUNC(changed), NULL);
    gtk_signal_connect(GTK_OBJECT(editor), "moved", GTK_SIGNAL_FUNC(moved), NULL);
    gtk_signal_connect(GTK_OBJECT(editor), "key_press_event", GTK_SIGNAL_FUNC(key_press), NULL);
    gtk_signal_connect(GTK_OBJECT(editor), "files_dropped", GTK_SIGNAL_FUNC(dropped), NULL);
    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled), editor);
    gtk_box_pack_start(GTK_BOX(box), scrolled, TRUE, TRUE, 0);
    gtk_widget_show_all(box);

    gtk_widget_realize(win);
    icon = gdk_pixmap_create_from_xpm_d(win->window, &icon_mask, NULL, cgedit_xpm);
    gtk_widget_grab_focus(editor);
    state();
}

static void
usage(const gchar *argv0)
{
    printf(_("Usage: %s [OPTION...] [FILE]\n"), argv0);
    printf("\n");
    printf(_("  --codeset=CODESET  open the file as text in this codeset\n"));
    printf(_("  --tab-width=WIDTH  make a tab this many spaces wide\n"));
    printf(_("  --jump=LINE        go to this line of the file\n"));
    printf(_("  --help             show this help and exit\n"));
    printf(_("  --version          show the version and exit\n"));
    printf("\n");
    printf(_("The usual GTK options, such as --display, are also understood.\n"));
}

/* What follows an option: after its = or as the next word. */
static const gchar *
value(gint argc, char **argv, gint *i, const gchar *option)
{
    gsize n = strlen(option);

    if (strncmp(argv[*i], option, n))
        return NULL;
    if (argv[*i][n] == '=')
        return argv[*i] + n + 1;
    if (!argv[*i][n] && *i + 1 < argc)
        return argv[++*i];
    return NULL;
}

int
main(int argc, char **argv)
{
    const gchar *name = NULL, *codeset = NULL, *v;
    gint tab = 0, jump = 0, i;
    gchar *full, *said, *cwd;
    gsize start;

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
            printf("cgedit %s\n", PACKAGE_VERSION);
            return 0;
        }

    gtk_init(&argc, &argv);
    for (i = 1; i < argc; i++)  /* what is left once GTK has taken its own */
        if ((v = value(argc, argv, &i, "--codeset")))
            codeset = v;
        else if ((v = value(argc, argv, &i, "--tab-width")))
            tab = atoi(v);
        else if ((v = value(argc, argv, &i, "--jump")))
            jump = atoi(v);
        else if (argv[i][0] != '-' || !argv[i][1])
            name = argv[i];
        else {
            fprintf(stderr, _("%s: unknown option %s\n"), argv[0], argv[i]);
            return 1;
        }
    if (codeset && !(full = g_convert("TEST", -1, "UTF-8", codeset, NULL, NULL, NULL))) {
        fprintf(stderr, _("%s: codeset '%s' is not supported\n"), argv[0], codeset);
        return 1;
    }

    gtk_window_set_default_icon_name("cgedit");
    settings_load();
    build();
    if (tab > 0)
        gtk_editor_set_tab_width(GTK_EDITOR(editor), tab);
    gtk_widget_show(win);
    if (name) {
        cwd = g_get_current_dir();
        full = !strncmp(name, "file:", 5) ? g_filename_from_uri(name, NULL, NULL)
            : g_path_is_absolute(name) ? g_strdup(name) : g_build_filename(cwd, name, NULL);
        g_free(cwd);
        if (!full) {
            said = g_strdup_printf(_("'%s' is not a file's name"), name);
            tell(GTK_WINDOW(win), said);
            g_free(said);
        } else if (load(full, codeset) && jump > 0) {
            start = gtk_editor_line_start(GTK_EDITOR(editor), jump - 1);
            gtk_editor_select(GTK_EDITOR(editor), start, start);
        }
        g_free(full);
    }
    gtk_main();
    return 0;
}
