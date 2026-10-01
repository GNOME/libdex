/* test-gio.c
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <glib/gstdio.h>

#include <libdex.h>

#include "test-util.h"

static void
assert_file_test (const char *path,
                  GFileTest   test)
{
  GError *error = NULL;
  gboolean actual;

  actual = dex_await_boolean (dex_file_test (path, test), &error);

  g_assert_no_error (error);
  g_assert_cmpint (actual, ==, g_file_test (path, test));
  g_clear_error (&error);
}

static gboolean
try_make_symbolic_link (const char *path,
                        const char *target)
{
  GFile *file = g_file_new_for_path (path);
  GError *error = NULL;
  gboolean created;

  created = g_file_make_symbolic_link (file, target, NULL, &error);
  g_object_unref (file);

  if (created)
    return TRUE;

  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED) ||
      g_error_matches (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED))
    {
      g_test_message ("Skipping symbolic link %s: %s", path, error->message);
      g_clear_error (&error);
      return FALSE;
    }

  g_assert_no_error (error);
  return FALSE;
}

static void
test_file_test (void)
{
  char *dir = NULL;
  char *regular = NULL;
  char *executable = NULL;
  char *link = NULL;
  char *dangling = NULL;
  char *missing = NULL;
  GError *error = NULL;
  const char *paths[6];
  guint n_paths = 0;
  gboolean have_link;
  gboolean have_dangling;
  const GFileTest tests[] = {
    G_FILE_TEST_EXISTS,
    G_FILE_TEST_IS_REGULAR,
    G_FILE_TEST_IS_DIR,
    G_FILE_TEST_IS_SYMLINK,
    G_FILE_TEST_IS_EXECUTABLE,
    (G_FILE_TEST_IS_SYMLINK | G_FILE_TEST_IS_REGULAR),
    (G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR),
  };

  dir = g_dir_make_tmp ("libdex-file-test-XXXXXX", &error);
  g_assert_no_error (error);
  regular = g_build_filename (dir, "regular", NULL);
  executable = g_build_filename (dir, "executable", NULL);
  link = g_build_filename (dir, "link", NULL);
  dangling = g_build_filename (dir, "dangling", NULL);
  missing = g_build_filename (dir, "missing", NULL);

  g_assert_true (g_file_set_contents (regular, "data", -1, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_set_contents (executable, "data", -1, &error));
  g_assert_no_error (error);
  g_assert_cmpint (g_chmod (regular, 0600), ==, 0);
  g_assert_cmpint (g_chmod (executable, 0700), ==, 0);

  have_link = try_make_symbolic_link (link, regular);
  have_dangling = try_make_symbolic_link (dangling, missing);

  paths[n_paths++] = dir;
  paths[n_paths++] = regular;
  paths[n_paths++] = executable;
  paths[n_paths++] = missing;
  if (have_link)
    paths[n_paths++] = link;
  if (have_dangling)
    paths[n_paths++] = dangling;

  for (guint i = 0; i < n_paths; i++)
    {
      for (guint j = 0; j < G_N_ELEMENTS (tests); j++)
        assert_file_test (paths[i], tests[j]);
    }

  if (have_dangling)
    g_assert_cmpint (g_unlink (dangling), ==, 0);
  if (have_link)
    g_assert_cmpint (g_unlink (link), ==, 0);
  g_assert_cmpint (g_unlink (executable), ==, 0);
  g_assert_cmpint (g_unlink (regular), ==, 0);
  g_assert_cmpint (g_rmdir (dir), ==, 0);

  g_free (missing);
  g_free (dangling);
  g_free (link);
  g_free (executable);
  g_free (regular);
  g_free (dir);
  g_clear_error (&error);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  _g_test_add_func ("/Dex/Gio/file-test", test_file_test);

  return g_test_run ();
}
