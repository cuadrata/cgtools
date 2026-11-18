/* cgrun-apps.c - cgrun's reading of .desktop files, its matching and its
   starting of programs, with no window

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
#include <unistd.h>

#include "cgrun.h"

static gint failed;

#define CHECK(what) \
    do { if (!(what)) { printf("line %d: %s\n", __LINE__, #what); failed++; } } while (0)

static App *
find(GList *apps, const gchar *id)
{
    for (; apps; apps = apps->next)
        if (!strcmp(((App *) apps->data)->id, id))
            return apps->data;
    return NULL;
}

/* The words as one string with bars between, which is easy to compare. */
static gchar *
barred(gchar **argv)
{
    gchar *all = argv ? g_strjoinv("|", argv) : g_strdup("(none)");

    g_strfreev(argv);
    return all;
}

/* What a program that was started wrote, once it has: it is not waited for. */
static gchar *
written(const gchar *file)
{
    gchar *text = NULL;
    gint i;

    for (i = 0; i < 100 && !(g_file_get_contents(file, &text, NULL, NULL) && *text); i++) {
        g_free(text);
        text = NULL;
        usleep(50000);
    }
    unlink(file);
    return text ? text : g_strdup("(nothing)");
}

int
main(void)
{
    GError *error = NULL;
    GList *apps;
    App *app, odd;
    gchar *s, *t, *key, *out, *line, cwd[1024], **argv;

    /* home is where the test runs, and what it starts writes there */
    if (!getcwd(cwd, sizeof cwd))
        return 99;
    setenv("HOME", cwd, 1);
    setenv("XDG_DATA_HOME", TESTDATA "/user", 1);
    setenv("XDG_DATA_DIRS", TESTDATA "/system:" TESTDATA "/absent", 1);
    setenv("XDG_CURRENT_DESKTOP", "More:Here", 1);

    /* which entries are programs to show */
    apps = apps_load();
    CHECK(g_list_length(apps) == 4);
    CHECK(find(apps, "editor.desktop") != NULL);
    CHECK(find(apps, "here.desktop") != NULL);
    CHECK(find(apps, "sub-inner.desktop") != NULL);
    CHECK(!find(apps, "hidden.desktop"));   /* NoDisplay */
    CHECK(!find(apps, "gone.desktop"));     /* the user's copy says Hidden */
    CHECK(!find(apps, "other.desktop"));    /* only for another desktop */
    CHECK(!find(apps, "nothere.desktop"));  /* not for this one */
    CHECK(!find(apps, "tryless.desktop"));  /* its program is not installed */
    CHECK(!find(apps, "link.desktop"));
    CHECK(!find(apps, "noexec.desktop"));
    CHECK(!find(apps, "broken.desktop"));
    app = find(apps, "mine.desktop");       /* the user's before the system's */
    CHECK(app && !strcmp(app->name, "Mine") && !strcmp(app->exec, "mine"));
    app = find(apps, "editor.desktop");
    CHECK(app && !strcmp(app->about, "Text Editor") && !app->terminal && !app->dir);

    /* how well a name is met */
    key = text_key("WRITES");
    CHECK(app && text_match(app->key, app->more, "") == 1);
    CHECK(app && text_match(app->key, app->more, "ed") == 4);      /* the name begins with it */
    CHECK(app && text_match(app->key, app->more, "dit") == 2);     /* the name holds it */
    CHECK(app && text_match(app->key, app->more, "write") == 1);   /* a keyword */
    CHECK(app && text_match(app->key, app->more, "text ed") == 1); /* every word, somewhere */
    CHECK(app && text_match(app->key, app->more, "ed zz") == 0);
    CHECK(app && text_match(app->key, app->more, key) == 0);       /* the comment is not searched */
    g_free(key);
    key = text_key("Inner THING");
    CHECK(text_match(key, NULL, "thing") == 3);                    /* a word of the name begins with it */
    CHECK(text_match(key, NULL, "inner thing") == 4);
    g_free(key);

    /* a program's command line */
    s = barred(app ? app_argv(app, NULL) : NULL);
    CHECK(!strcmp(s, "editor|--new"));
    g_free(s);
    app = find(apps, "sub-inner.desktop");
    CHECK(app && app->terminal && !strcmp(app->dir, "/tmp"));
    s = barred(app ? app_argv(app, NULL) : NULL);
    CHECK(!strcmp(s, "inner|a b|--name=Inner Thing|" TESTDATA "/system/applications/sub/inner.desktop|100%"));
    g_free(s);
    memset(&odd, 0, sizeof odd);
    odd.name = "Odd";
    odd.file = "odd.desktop";
    odd.exec = "%U";
    CHECK(!app_argv(&odd, &error) && error);
    g_clear_error(&error);
    odd.exec = "odd \"unclosed";
    CHECK(!app_argv(&odd, &error) && error);
    g_clear_error(&error);

    /* a command line that was typed */
    s = barred(line_argv("play ~/a\\ b ~ ~x x~/y", NULL));
    t = g_strdup_printf("play|%s/a b|%s|~x|x~/y", cwd, cwd);
    CHECK(!strcmp(s, t));
    g_free(t);
    g_free(s);
    CHECK(!line_argv("play 'unclosed", &error) && error);
    g_clear_error(&error);

    /* starting it: where it is told to, at home otherwise, and in a terminal */
    out = g_build_filename(cwd, "cgrun-apps.tmp", NULL);
    line = g_strdup_printf("sh -c 'pwd > \"%s\"'", out);
    argv = line_argv(line, NULL);
    CHECK(argv && run_argv(argv, "/", FALSE, &error) && !error);
    s = written(out);
    CHECK(!strcmp(s, "/\n"));
    g_free(s);
    CHECK(argv && run_argv(argv, "/no/such/directory", FALSE, &error) && !error);
    s = written(out);
    CHECK(strlen(s) > 1 && !strncmp(s, cwd, strlen(s) - 1));
    g_free(s);
    g_strfreev(argv);
    g_free(line);

    line = g_strdup_printf("sh -c 'echo \"$@\" > \"%s\"' term", out);
    setenv("TERMINAL", line, 1);
    argv = line_argv("top -d 1", NULL);
    CHECK(argv && run_argv(argv, NULL, TRUE, &error) && !error);
    s = written(out);
    CHECK(!strcmp(s, "-e top -d 1\n"));
    g_free(s);
    g_strfreev(argv);
    g_free(line);

    argv = line_argv("cgrun-no-such-program --at-all", NULL);
    CHECK(argv && !run_argv(argv, NULL, FALSE, &error) && error);
    g_clear_error(&error);
    g_strfreev(argv);

    g_free(out);
    apps_free(apps);
    if (failed)
        printf("%d failed\n", failed);
    return failed != 0;
}
