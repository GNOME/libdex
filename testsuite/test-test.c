/* test-test.c
 *
 * Copyright 2026 Christian Hergert
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
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <libdex.h>

static gboolean did_flush;

static gboolean
mark_flushed_cb (gpointer user_data)
{
  did_flush = TRUE;

  return G_SOURCE_REMOVE;
}

static gboolean
resolve_cb (gpointer user_data)
{
  DexPromise *promise = user_data;

  dex_promise_resolve_boolean (promise, TRUE);
  dex_unref (promise);

  return G_SOURCE_REMOVE;
}

static void
test_test_add_func (void)
{
  GError * error = NULL;
  DexPromise * promise = NULL;

  g_assert_true (DEX_IS_SCHEDULER (dex_scheduler_get_thread_default ()));

  promise = dex_promise_new ();
  g_idle_add (resolve_cb, dex_ref (promise));

  g_assert_true (dex_await (dex_ref (promise), &error));
  g_assert_no_error (error);

  g_idle_add (mark_flushed_cb, NULL);
}

static void
test_test_add_func_flushed (void)
{
  g_assert_true (did_flush);
}

int
main (int   argc,
      char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);
  dex_test_add_func ("/Dex/TestSuite/Test/add_func", test_test_add_func);
  g_test_add_func ("/Dex/TestSuite/Test/add_func/flushed", test_test_add_func_flushed);
  return g_test_run ();
}
