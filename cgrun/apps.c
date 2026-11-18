/* apps.c - the programs there are to run, and the running of them

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

#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cgrun.h"

#define ENTRY "Desktop Entry"

/* Text as it is searched: without the difference of capitals. */
gchar *
text_key(const gchar *text)
{
    return g_utf8_validate(text, -1, NULL) ? g_utf8_casefold(text, -1) : g_strdup(text);
}

/*
 * How well what is wanted is met by a thing that has a name, key, and
 * may have more to it: 0 is not at all. Every word that is wanted must
 * be there somewhere. The name beginning with what is wanted is the
 * best, then a word of the name beginning with it, then the name
 * holding it, and last the rest.
 */
gint
text_match(const gchar *key, const gchar *more, const gchar *want)
{
    gchar **words = g_strsplit(want, " ", 0);
    const gchar *at;
    gint i, score = 1;

    for (i = 0; words[i] && score; i++)
        if (*words[i] && !strstr(key, words[i]) && !(more && strstr(more, words[i])))
            score = 0;
    g_strfreev(words);
    if (!score || !*want)
        return score;
    for (at = strstr(key, want); at; at = strstr(at + 1, want)) {
        if (at == key)
            return 4;
        score = strchr(" -_./(", at[-1]) ? 3 : MAX(score, 2);
        if (score == 3)
            break;
    }
    return score;
}

static gchar *
value(GKeyFile *kf, const gchar *key, gboolean local)
{
    gchar *v = local ? g_key_file_get_locale_string(kf, ENTRY, key, NULL, NULL)
                     : g_key_file_get_string(kf, ENTRY, key, NULL);

    if (v && !*v) {
        g_free(v);
        v = NULL;
    }
    return v;
}

/* Whether one of the desktops the entry names is the one that runs. */
static gboolean
named(GKeyFile *kf, const gchar *key)
{
    const gchar *desktop = g_getenv("XDG_CURRENT_DESKTOP");
    gchar **theirs = g_key_file_get_string_list(kf, ENTRY, key, NULL, NULL);
    gchar **ours = g_strsplit(desktop ? desktop : "", ":", 0);
    gboolean is = FALSE;
    gint i, j;

    for (i = 0; theirs && theirs[i]; i++)
        for (j = 0; ours[j]; j++)
            if (*ours[j] && !strcmp(theirs[i], ours[j]))
                is = TRUE;
    g_strfreev(theirs);
    g_strfreev(ours);
    return is;
}

static void
app_free(App *app)
{
    g_free(app->id);
    g_free(app->file);
    g_free(app->name);
    g_free(app->about);
    g_free(app->exec);
    g_free(app->dir);
    g_free(app->key);
    g_free(app->more);
    g_free(app);
}

/* The entry in the file, if it is a program that is to be shown. */
static App *
app_read(const gchar *file, const gchar *id)
{
    GKeyFile *kf = g_key_file_new();
    gchar *type = NULL, *try = NULL, *found = NULL, *plain, *generic, *words, *prog, *all;
    App *app = NULL;

    if (!g_key_file_load_from_file(kf, file, G_KEY_FILE_NONE, NULL))
        goto out;
    type = value(kf, "Type", FALSE);
    try = value(kf, "TryExec", FALSE);
    if (!type || strcmp(type, "Application") ||
        g_key_file_get_boolean(kf, ENTRY, "Hidden", NULL) ||
        g_key_file_get_boolean(kf, ENTRY, "NoDisplay", NULL) ||
        (g_key_file_has_key(kf, ENTRY, "OnlyShowIn", NULL) && !named(kf, "OnlyShowIn")) ||
        named(kf, "NotShowIn") ||
        (try && !(found = g_find_program_in_path(try))))
        goto out;

    app = g_new0(App, 1);
    app->name = value(kf, "Name", TRUE);
    app->exec = value(kf, "Exec", FALSE);
    if (!app->name || !app->exec) {
        app_free(app);
        app = NULL;
        goto out;
    }
    app->id = g_strdup(id);
    app->file = g_strdup(file);
    app->dir = value(kf, "Path", FALSE);
    app->terminal = g_key_file_get_boolean(kf, ENTRY, "Terminal", NULL);
    generic = value(kf, "GenericName", TRUE);
    app->about = generic ? g_strdup(generic) : value(kf, "Comment", TRUE);
    app->key = text_key(app->name);

    /* It is found by its name in English too, by what kind of thing it
     * is, by the words it gives to be found by, and by its program. */
    plain = value(kf, "Name", FALSE);
    words = value(kf, "Keywords", TRUE);
    all = g_strndup(app->exec, strcspn(app->exec, " "));
    prog = g_path_get_basename(all);
    g_free(all);
    all = g_strjoin("\n", plain ? plain : "", generic ? generic : "", words ? words : "", prog, NULL);
    app->more = text_key(all);
    g_free(all);
    g_free(prog);
    g_free(words);
    g_free(plain);
    g_free(generic);
out:
    g_free(type);
    g_free(try);
    g_free(found);
    g_key_file_free(kf);
    return app;
}

/*
 * The entries under one applications directory. A file's id is its
 * path from there with a dash for each slash, and of two files with
 * one id the first that is come to is the one: it may be there to say
 * that the other is not to be shown.
 */
static void
scan(const gchar *top, const gchar *sub, gint depth, GHashTable *seen, GList **apps)
{
    gchar *dir = g_build_filename(top, sub, NULL);
    DIR *d = opendir(dir);
    struct dirent *e;

    while (d && (e = readdir(d))) {
        gsize len = strlen(e->d_name);
        gchar *rel, *file, *id;
        struct stat st;
        App *app;

        if (e->d_name[0] == '.')
            continue;
        rel = sub ? g_build_filename(sub, e->d_name, NULL) : g_strdup(e->d_name);
        file = g_build_filename(top, rel, NULL);
        if (stat(file, &st) != 0)
            ;
        else if (S_ISDIR(st.st_mode)) {
            if (depth < 4)
                scan(top, rel, depth + 1, seen, apps);
        } else if (S_ISREG(st.st_mode) && len > 8 && !strcmp(e->d_name + len - 8, ".desktop")) {
            id = g_strdelimit(g_strdup(rel), "/", '-');
            if (g_hash_table_lookup(seen, id))
                g_free(id);
            else {
                g_hash_table_insert(seen, id, id);
                if ((app = app_read(file, id)))
                    *apps = g_list_prepend(*apps, app);
            }
        }
        g_free(file);
        g_free(rel);
    }
    if (d)
        closedir(d);
    g_free(dir);
}

static void
seen_free(gpointer key, gpointer value, gpointer data)
{
    g_free(key);
}

/* Every program there is to show, the user's own before the system's. */
GList *
apps_load(void)
{
    GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
    const gchar *const *dirs = g_get_system_data_dirs();
    GList *apps = NULL;
    gchar *dir;
    gint i;

    for (i = -1; i < 0 || dirs[i]; i++) {
        dir = g_build_filename(i < 0 ? g_get_user_data_dir() : dirs[i], "applications", NULL);
        scan(dir, NULL, 0, seen, &apps);
        g_free(dir);
    }
    g_hash_table_foreach(seen, seen_free, NULL);
    g_hash_table_destroy(seen);
    return apps;
}

void
apps_free(GList *apps)
{
    GList *l;

    for (l = apps; l; l = l->next)
        app_free(l->data);
    g_list_free(apps);
}

/*
 * The words of the program's command line. The codes in it that stand
 * for files to open are left out, as there are none; %c is its name,
 * %k its file and %% a per cent sign.
 */
gchar **
app_argv(const App *app, GError **error)
{
    gchar **words, **argv;
    gint n, i, j = 0;

    if (!g_shell_parse_argv(app->exec, &n, &words, error))
        return NULL;
    argv = g_new0(gchar *, n + 1);
    for (i = 0; i < n; i++) {
        GString *s = g_string_new(NULL);
        const gchar *p;

        for (p = words[i]; *p; p++)
            if (*p != '%')
                g_string_append_c(s, *p);
            else if (!*++p)
                break;
            else if (*p == '%')
                g_string_append_c(s, '%');
            else if (*p == 'c')
                g_string_append(s, app->name);
            else if (*p == 'k')
                g_string_append(s, app->file);
        if (s->len || !strchr(words[i], '%'))
            argv[j++] = g_strdup(s->str);
        g_string_free(s, TRUE);
    }
    g_strfreev(words);
    if (!j) {
        g_free(argv);
        g_shell_parse_argv("", NULL, NULL, error);  /* for what it says of nothing */
        return NULL;
    }
    return argv;
}

/*
 * The words of a command line that was typed, taken apart as a shell
 * takes them, but not by one: of all a shell would do with them only
 * a tilde at the start of a word is seen to, which is home.
 */
gchar **
line_argv(const gchar *line, GError **error)
{
    gchar **argv, *word;
    gint i;

    if (!g_shell_parse_argv(line, NULL, &argv, error))
        return NULL;
    for (i = 0; argv[i]; i++)
        if (argv[i][0] == '~' && (!argv[i][1] || argv[i][1] == '/')) {
            word = g_strconcat(g_get_home_dir(), argv[i] + 1, NULL);
            g_free(argv[i]);
            argv[i] = word;
        }
    return argv;
}

/* What is started is no part of the session of what started it. */
static void
apart(gpointer data)
{
    setsid();
}

/*
 * Starts the words as a program, in the directory if there is one and
 * at home if not, and in a terminal if it wants one: the one that
 * TERMINAL names, or cgterm, or xterm, each of which runs what comes
 * after -e. It is not waited for.
 */
gboolean
run_argv(gchar **argv, const gchar *dir, gboolean terminal, GError **error)
{
    const gchar *named = g_getenv("TERMINAL");
    gchar **term = NULL, **all = argv, *found;
    gint n = 0, i;
    gboolean ok;

    if (terminal) {
        if (!named || !g_shell_parse_argv(named, &n, &term, NULL)) {
            found = g_find_program_in_path("cgterm");
            term = g_new0(gchar *, 2);
            term[0] = g_strdup(found ? "cgterm" : "xterm");
            n = 1;
            g_free(found);
        }
        all = g_new0(gchar *, n + g_strv_length(argv) + 2);
        for (i = 0; i < n; i++)
            all[i] = term[i];
        all[n] = "-e";
        for (i = 0; argv[i]; i++)
            all[n + 1 + i] = argv[i];
    }
    ok = g_spawn_async(dir && g_file_test(dir, G_FILE_TEST_IS_DIR) ? dir : g_get_home_dir(),
                       all, NULL, G_SPAWN_SEARCH_PATH, apart, NULL, NULL, error);
    if (terminal) {
        g_free(all);
        g_strfreev(term);
    }
    return ok;
}
