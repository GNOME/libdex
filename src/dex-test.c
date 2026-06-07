/*
 * dex-test.c
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

#include "dex-test.h"

#include "dex-future.h"
#include "dex-init.h"
#include "dex-main-scheduler-private.h"
#include "dex-scheduler-private.h"
#include "dex-thread-storage-private.h"

typedef struct _DexTest
{
  GTestFunc test_func;
} DexTest;

static DexFuture *
dex_test_runner_fiber (gpointer user_data)
{
  DexTest *test = user_data;

  g_assert (test != NULL);
  g_assert (test->test_func != NULL);

  test->test_func ();

  return dex_future_new_true ();
}

static DexScheduler *
dex_test_ensure_thread_scheduler (GMainContext *main_context,
                                  gboolean     *created_scheduler)
{
  DexScheduler *scheduler;

  g_assert (main_context != NULL);
  g_assert (created_scheduler != NULL);

  *created_scheduler = FALSE;

  if ((scheduler = dex_scheduler_get_thread_default ()))
    return dex_ref (scheduler);

  *created_scheduler = TRUE;

  return DEX_SCHEDULER (dex_main_scheduler_new (main_context));
}

static void
dex_test_runner (gconstpointer user_data)
{
  DexTest *test = (gpointer)user_data;
  DexScheduler *scheduler = NULL;
  DexFuture *future = NULL;
  GMainContext *main_context = NULL;
  GError *error = NULL;
  const GValue *value;
  gboolean created_scheduler;

  dex_init ();

  main_context = g_main_context_ref_thread_default ();
  scheduler = dex_test_ensure_thread_scheduler (main_context, &created_scheduler);

  future = dex_scheduler_spawn (scheduler,
                                8 * 1024 * 1024,
                                dex_test_runner_fiber,
                                test,
                                NULL);

  while (dex_future_is_pending (future))
    g_main_context_iteration (main_context, TRUE);

  value = dex_future_get_value (future, &error);
  g_assert_no_error (error);
  g_assert_nonnull (value);

  while (g_main_context_pending (main_context))
    g_main_context_iteration (main_context, FALSE);

  if (created_scheduler)
    {
      DexThreadStorage *storage = dex_thread_storage_get ();

      storage->scheduler = NULL;
      storage->aio_context = NULL;
    }

  g_clear_error (&error);
  g_clear_pointer (&future, dex_unref);
  g_clear_pointer (&scheduler, dex_unref);
  g_clear_pointer (&main_context, g_main_context_unref);
}

/**
 * dex_test_add_func:
 * @testpath: test case path
 * @test_func: (scope async): test function to execute from a fiber
 *
 * Adds a test function like g_test_add_func(), but runs @test_func from a
 * [class@Dex.Fiber]. The calling thread is given a thread-default
 * [class@Dex.Scheduler] if it does not already have one, allowing tests to use
 * dex_await() and related APIs directly.
 *
 * After @test_func completes, the scheduler's main context is iterated until no
 * immediately pending sources remain.
 */
void
dex_test_add_func (const char *testpath,
                   GTestFunc   test_func)
{
  DexTest *test;

  g_return_if_fail (testpath != NULL);
  g_return_if_fail (test_func != NULL);

  test = g_new0 (DexTest, 1);
  test->test_func = test_func;

  g_test_add_data_func_full (testpath, test, dex_test_runner, g_free);
}
