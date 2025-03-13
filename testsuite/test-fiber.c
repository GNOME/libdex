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
  DexFiber *fiber = dex_fiber_new (scheduler_fiber_func, NULL, NULL, 0);
  const GValue *value;

  g_source_attach ((GSource *)fiber_scheduler, NULL);

  test_arg = 0;
  dex_future_set_static_name (DEX_FUTURE (fiber), "fiber_func");
  dex_fiber_scheduler_register (fiber_scheduler, fiber);
  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);
  g_assert_cmpint (test_arg, ==, 99);

  ASSERT_STATUS (fiber, DEX_FUTURE_STATUS_RESOLVED);
  value = dex_future_get_value (DEX_FUTURE (fiber), NULL);
  g_assert_cmpint (99, ==, g_value_get_int (value));
  dex_clear (&fiber);

  fiber = dex_fiber_new (scheduler_fiber_error, NULL, NULL, 0);
  dex_future_set_static_name (DEX_FUTURE (fiber), "fiber_error");
  dex_fiber_scheduler_register (fiber_scheduler, fiber);
  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);
  ASSERT_ERROR (fiber, DEX_ERROR, DEX_ERROR_FIBER_EXITED);
  dex_clear (&fiber);

  g_source_destroy ((GSource *)fiber_scheduler);
  g_source_unref ((GSource *)fiber_scheduler);
}

static DexFuture *
test_await_send (gpointer user_data)
{
  DexChannel *channel = user_data;

  for (guint i = 0; i < 4; i++)
    {
      DexFuture *send;
      const GValue *value;
      GError *error = NULL;

      send = dex_channel_send (channel, dex_future_new_for_int (200));
      g_assert_true (dex_await (dex_ref (send), &error));
      g_assert_no_error (error);
      value = dex_future_get_value (send, NULL);
      g_assert_nonnull (value);
      g_assert_true (G_VALUE_HOLDS_UINT (value));
      dex_unref (send);
    }

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
test_await_recv (gpointer user_data)
{
  DexChannel *channel = user_data;

  for (guint i = 0; i < 4; i++)
    {
      DexFuture *recv;
      GError *error = NULL;
      const GValue *value;

      recv = dex_channel_receive (channel);
      g_assert_true (dex_await (dex_ref (recv), &error));
      g_assert_no_error (error);
      value = dex_future_get_value (recv, NULL);
      g_assert_nonnull (value);
      g_assert_true (G_VALUE_HOLDS_INT (value));
      g_assert_cmpint (200, ==, g_value_get_int (value));
      dex_unref (recv);
    }

  return dex_future_new_for_boolean (TRUE);
}

static void
test_fiber_scheduler_await (void)
{
  DexFiberScheduler *fiber_scheduler = dex_fiber_scheduler_new ();
  DexChannel *channel = dex_channel_new (0);
  DexFiber *fiber1 = dex_fiber_new (test_await_send, dex_ref (channel), dex_unref, 0);
  DexFiber *fiber2 = dex_fiber_new (test_await_recv, dex_ref (channel), dex_unref, 0);

  dex_future_set_static_name (DEX_FUTURE (fiber1), "fiber1");
  dex_future_set_static_name (DEX_FUTURE (fiber2), "fiber2");

  dex_fiber_scheduler_register (fiber_scheduler, fiber2);
  dex_fiber_scheduler_register (fiber_scheduler, fiber1);

  g_source_attach ((GSource *)fiber_scheduler, NULL);

  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);

  ASSERT_STATUS (fiber1, DEX_FUTURE_STATUS_RESOLVED);
  ASSERT_STATUS (fiber2, DEX_FUTURE_STATUS_RESOLVED);

  dex_clear (&fiber1);
  dex_clear (&fiber2);
  dex_clear (&channel);

  g_source_destroy ((GSource *)fiber_scheduler);
  g_source_unref ((GSource *)fiber_scheduler);
}

static DexFuture *
test_fiber_cancel_propagate_fiber (gpointer data)
{
  DexFuture *conditional = data;
  GError *error = NULL;
  gboolean ret;

  g_assert (DEX_IS_PROMISE (conditional));

  ret = dex_await (dex_ref (conditional), &error);
  g_assert_false (ret);
  g_assert_error (error, DEX_ERROR, DEX_ERROR_FIBER_CANCELLED);

  return dex_future_new_for_error (g_steal_pointer (&error));
}

static DexFuture *
after_fiber_cancelled (DexFuture *future,
                       gpointer   user_data)
{
  g_assert_not_reached ();
  return NULL;
}

static void
test_fiber_cancel_propagate (void)
{
  DexFiberScheduler *fiber_scheduler = dex_fiber_scheduler_new ();
  DexPromise *conditional = dex_promise_new ();
  DexFiber *fiber = dex_fiber_new (test_fiber_cancel_propagate_fiber, dex_ref (conditional), dex_unref, 0);
  DexFuture *block = dex_future_then (dex_ref (DEX_FUTURE (fiber)), after_fiber_cancelled, NULL, NULL);

  dex_fiber_scheduler_register (fiber_scheduler, fiber);

  g_source_attach ((GSource *)fiber_scheduler, NULL);

  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);

  ASSERT_STATUS (fiber, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (block, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (conditional, DEX_FUTURE_STATUS_PENDING);

  dex_clear (&block);

  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);

  ASSERT_STATUS (fiber, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (conditional, DEX_FUTURE_STATUS_PENDING);

  dex_promise_resolve_boolean (conditional, TRUE);

  ASSERT_STATUS (conditional, DEX_FUTURE_STATUS_RESOLVED);

  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);

  dex_clear (&fiber);
  dex_clear (&conditional);

  g_source_destroy ((GSource *)fiber_scheduler);
  g_source_unref ((GSource *)fiber_scheduler);
}

int
main (int argc,
      char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Dex/TestSuite/FiberScheduler/basic", test_fiber_scheduler_basic);
  g_test_add_func ("/Dex/TestSuite/FiberScheduler/await", test_fiber_scheduler_await);
  g_test_add_func ("/Dex/TestSuite/FiberScheduler/cancel_propagate", test_fiber_cancel_propagate);
  return g_test_run ();
}
