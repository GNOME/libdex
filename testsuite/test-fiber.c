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

static int test_arg = 123;
static ucontext_t g_context;
static GRecMutex rmutex;

static void
fiber_func (DexFiber *fiber,
            gpointer  user_data)
{
  int *arg = user_data;

  g_assert_true (user_data == &test_arg);
  g_assert_cmpint (*arg, ==, 123);
  *arg = 321;
  swapcontext (&fiber->context, &g_context);
}

static void
test_fiber_basic (void)
{
  DexFiber *fiber;

  fiber = dex_fiber_new (fiber_func, &test_arg, 4096);
  swapcontext (&g_context, &fiber->context);
  g_assert_cmpint (test_arg, ==, 321);

  dex_unref (fiber);
}

static void
fiber_rec_func (DexFiber *fiber,
                gpointer  user_data)
{
  g_rec_mutex_lock (&rmutex);
  g_rec_mutex_lock (&rmutex);
  g_rec_mutex_unlock (&rmutex);
  g_rec_mutex_unlock (&rmutex);

  swapcontext (&fiber->context, &g_context);
}

static void
test_fiber_rec_mutex (void)
{
  DexFiber *fiber;

  g_rec_mutex_init (&rmutex);
  g_rec_mutex_lock (&rmutex);

  fiber = dex_fiber_new (fiber_rec_func, NULL, 0);
  swapcontext (&g_context, &fiber->context);
  dex_unref (fiber);

  g_rec_mutex_unlock (&rmutex);
  g_rec_mutex_clear (&rmutex);
}

static void
scheduler_fiber_func (DexFiber *fiber,
                      gpointer  user_data)
{
  test_arg = 99;
}

static void
test_fiber_scheduler_basic (void)
{
  DexFiberScheduler *fiber_scheduler = dex_fiber_scheduler_new ();
  DexFiber *fiber = dex_fiber_new (scheduler_fiber_func, NULL, 0);

  g_source_attach ((GSource *)fiber_scheduler, NULL);

  test_arg = 0;
  dex_fiber_migrate_to (fiber, fiber_scheduler);
  g_main_context_iteration (NULL, FALSE);
  g_assert_cmpint (test_arg, ==, 99);
}

int
main (int argc,
      char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Dex/TestSuite/Fiber/basic", test_fiber_basic);
  g_test_add_func ("/Dex/TestSuite/Fiber/rec-mutex", test_fiber_rec_mutex);
  g_test_add_func ("/Dex/TestSuite/FiberScheduler/basic", test_fiber_scheduler_basic);
  return g_test_run ();
}
