/* test-fiber.c
 *
 * Copyright 2022 Christian Hergert <chergert@redhat.com>
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

#include <ucontext.h>
#include <unistd.h>

#include <libdex.h>

#include "dex-fiber-private.h"

#define ASSERT_STATUS(f,status) g_assert_cmpint(status, ==, dex_future_get_status(DEX_FUTURE(f)))
#define ASSERT_ERROR(f,d,c) \
  G_STMT_START { \
    GError *_error = NULL; \
    ASSERT_STATUS (f, DEX_FUTURE_STATUS_REJECTED); \
    g_assert_null (dex_future_get_value (DEX_FUTURE (f), &_error)); \
    g_assert_cmpint (_error->domain, ==, d); \
    g_assert_cmpint (_error->code, ==, c); \
    g_clear_error (&_error); \
  } G_STMT_END

static int test_arg = 123;
static ucontext_t g_context;
static GRecMutex rmutex;

static DexFuture *
fiber_rec_func (gpointer user_data)
{
  DexFiber **fiber = user_data;

  g_rec_mutex_lock (&rmutex);
  g_rec_mutex_lock (&rmutex);
  g_rec_mutex_unlock (&rmutex);
  g_rec_mutex_unlock (&rmutex);

  swapcontext (&(*fiber)->context, &g_context);

  return NULL;
}

static void
test_fiber_rec_mutex (void)
{
  DexFiber *fiber;

  g_rec_mutex_init (&rmutex);
  g_rec_mutex_lock (&rmutex);

  fiber = dex_fiber_new (fiber_rec_func, &fiber, 0);
  swapcontext (&g_context, &fiber->context);
  ASSERT_STATUS (fiber, DEX_FUTURE_STATUS_PENDING);
  dex_unref (fiber);

  g_rec_mutex_unlock (&rmutex);
  g_rec_mutex_clear (&rmutex);
}

static DexFuture *
scheduler_fiber_func (gpointer user_data)
{
  test_arg = 99;
  return dex_future_new_for_int (99);
}

static DexFuture *
scheduler_fiber_error (gpointer user_data)
{
  return NULL;
}

static void
test_fiber_scheduler_basic (void)
{
  DexFiberScheduler *fiber_scheduler = dex_fiber_scheduler_new ();
  DexFiber *fiber = dex_fiber_new (scheduler_fiber_func, NULL, 0);
  const GValue *value;

  g_source_attach ((GSource *)fiber_scheduler, NULL);

  test_arg = 0;
  dex_fiber_migrate_to (fiber, fiber_scheduler);
  g_main_context_iteration (NULL, FALSE);
  g_assert_cmpint (test_arg, ==, 99);

  ASSERT_STATUS (fiber, DEX_FUTURE_STATUS_RESOLVED);
  value = dex_future_get_value (DEX_FUTURE (fiber), NULL);
  g_assert_cmpint (99, ==, g_value_get_int (value));
  dex_clear (&fiber);

  fiber = dex_fiber_new (scheduler_fiber_error, NULL, 0);
  dex_fiber_migrate_to (fiber, fiber_scheduler);
  g_main_context_iteration (NULL, FALSE);
  ASSERT_ERROR (fiber, DEX_ERROR, DEX_ERROR_FIBER_EXITED);
  dex_clear (&fiber);

  g_source_destroy ((GSource *)fiber_scheduler);
  g_source_unref ((GSource *)fiber_scheduler);
}

int
main (int argc,
      char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Dex/TestSuite/Fiber/rec-mutex", test_fiber_rec_mutex);
  g_test_add_func ("/Dex/TestSuite/FiberScheduler/basic", test_fiber_scheduler_basic);
  return g_test_run ();
}
