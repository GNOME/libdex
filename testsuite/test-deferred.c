/* test-deferred.c
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

#include <libdex.h>

#include "dex-future-private.h"
#include "dex-thread-storage-private.h"

typedef struct
{
  int *calls;
  int *destroyed;
} TestState;

static void
state_free (gpointer data)
{
  TestState *state = data;

  g_atomic_int_inc (state->destroyed);
  g_free (state);
}

static TestState *
state_new (int *calls,
           int *destroyed)
{
  TestState *state = g_new0 (TestState, 1);

  state->calls = calls;
  state->destroyed = destroyed;
  return state;
}

static DexFuture *
return_value (gpointer data)
{
  TestState *state = data;

  g_atomic_int_inc (state->calls);
  return dex_future_new_for_int (42);
}

static DexFuture *
return_null (gpointer data)
{
  return NULL;
}

static DexFuture *
coroutine_value (DexCoroutineContext *context,
                 gpointer             data)
{
  return return_value (data);
}

static DexFuture *
return_promise (gpointer data)
{
  return dex_ref (data);
}

static DexFuture *
pass_through (DexFuture *completed,
              gpointer   data)
{
  return dex_ref (completed);
}

static DexFuture *
await_deferred (gpointer data)
{
  DexDeferred *deferred = data;
  GError *error = NULL;
  int value;

  value = dex_await_int (dex_ref (DEX_FUTURE (deferred)), &error);
  if (error != NULL)
    return dex_future_new_for_error (error);

  return dex_future_new_for_int (value);
}

static void
wait_for (DexFuture *future)
{
  gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

  while (dex_future_is_pending (future) && g_get_monotonic_time () < deadline)
    g_main_context_iteration (NULL, FALSE);

  g_assert_false (dex_future_is_pending (future));
}

static void
test_deferred_kinds (void)
{
  DexThreadPool *pool = dex_thread_pool_new (1);
  DexDeferred *deferred[5];
  int calls = 0;
  int destroyed = 0;

  deferred[0] = dex_deferred_new (NULL, return_value,
                                  state_new (&calls, &destroyed), state_free);
  deferred[1] = dex_deferred_new_fiber (NULL, 0, return_value,
                                        state_new (&calls, &destroyed), state_free);
  deferred[2] = dex_deferred_new_coroutine (NULL, coroutine_value,
                                            state_new (&calls, &destroyed), state_free);
  deferred[3] = dex_deferred_new_thread ("deferred-test", return_value,
                                         state_new (&calls, &destroyed), state_free);
  deferred[4] = dex_deferred_new_on_thread_pool (pool, "deferred-test", return_value,
                                                 state_new (&calls, &destroyed), state_free);

  g_assert_cmpint (g_atomic_int_get (&calls), ==, 0);
  for (guint i = 0; i < G_N_ELEMENTS (deferred); i++)
    {
      DexFuture *first;
      DexFuture *second;
      const GValue *value;
      GError *error = NULL;

      g_assert_true (dex_future_is_pending (DEX_FUTURE (deferred[i])));
      value = dex_future_get_value (DEX_FUTURE (deferred[i]), &error);
      g_assert_null (value);
      g_assert_error (error, DEX_ERROR, DEX_ERROR_PENDING);
      g_clear_error (&error);
      g_assert_cmpint (g_atomic_int_get (&calls), ==, i);

      first = dex_future_then (dex_ref (DEX_FUTURE (deferred[i])),
                               pass_through, NULL, NULL);
      second = dex_future_then (dex_ref (DEX_FUTURE (deferred[i])),
                                pass_through, NULL, NULL);

      wait_for (first);
      wait_for (second);
      value = dex_future_get_value (DEX_FUTURE (deferred[i]), &error);
      g_assert_no_error (error);
      g_assert_nonnull (value);
      g_assert_cmpint (g_value_get_int (value), ==, 42);
      dex_unref (first);
      dex_unref (second);
      dex_unref (deferred[i]);
    }

  {
    gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

    while (g_atomic_int_get (&destroyed) < 5 && g_get_monotonic_time () < deadline)
      {
        while (g_main_context_pending (NULL))
          g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
      }
  }

  g_assert_cmpint (g_atomic_int_get (&calls), ==, 5);
  g_assert_cmpint (g_atomic_int_get (&destroyed), ==, 5);
  dex_unref (pool);
}

static void
test_deferred_closed_thread_pool (void)
{
  DexThreadPool *pool = dex_thread_pool_new (1);
  DexFuture *closed;
  DexDeferred *deferred;
  DexFuture *future;
  GError *error = NULL;
  int calls = 0;
  int destroyed = 0;

  closed = dex_thread_pool_close (pool, DEX_THREAD_POOL_SHUTDOWN_DRAIN);
  deferred = dex_deferred_new_on_thread_pool (pool, "deferred-test", return_value,
                                              state_new (&calls, &destroyed), state_free);

  /* Transfer the only reference: observing the deferred rejects it synchronously. */
  future = dex_future_then (DEX_FUTURE (deferred), pass_through, NULL, NULL);

  g_assert_true (dex_future_is_rejected (future));
  g_assert_null (dex_future_get_value (future, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_SEMAPHORE_CLOSED);
  g_assert_cmpint (g_atomic_int_get (&calls), ==, 0);
  g_assert_cmpint (g_atomic_int_get (&destroyed), ==, 1);

  wait_for (closed);
  g_clear_error (&error);
  dex_unref (future);
  dex_unref (closed);
  dex_unref (pool);
}

static void
test_deferred_no_result (void)
{
  DexDeferred *deferred = dex_deferred_new (NULL, return_null, NULL, NULL);
  DexFuture *future;
  GError *error = NULL;

  future = dex_future_then (dex_ref (DEX_FUTURE (deferred)), pass_through, NULL, NULL);
  wait_for (future);

  g_assert_null (dex_future_get_value (future, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_NO_RESULT);
  g_assert_cmpstr (error->message, ==, "Deferred callback returned NULL");

  g_clear_error (&error);
  dex_unref (future);
  dex_unref (deferred);
}

static DexFuture *
native_observer (gpointer data)
{
  DexThreadStorage *storage = NULL;
  DexDeferred *deferred;

  g_assert_null (dex_thread_storage_peek ());

  if (GPOINTER_TO_INT (data))
    {
      storage = dex_thread_storage_get ();
      g_assert_null (storage->scheduler);
    }

  /* Capturing the fallback scheduler must not allocate thread storage. */
  deferred = dex_deferred_new (NULL, return_promise, dex_future_new_for_int (42), dex_unref);
  g_assert_true (dex_thread_storage_peek () == storage);
  dex_unref (deferred);

  for (guint i = 0; i < 2; i++)
    {
      GError *error = NULL;
      const GValue *value;

      deferred = dex_deferred_new_thread ("deferred-native-observer",
                                          return_promise,
                                          dex_future_new_for_int (i),
                                          dex_unref);

      g_assert_true (dex_thread_wait_for (dex_ref (DEX_FUTURE (deferred)), &error));
      g_assert_no_error (error);
      value = dex_future_get_value (DEX_FUTURE (deferred), &error);
      g_assert_no_error (error);
      g_assert_nonnull (value);
      g_assert_cmpint (g_value_get_int (value), ==, i);
      dex_unref (deferred);
    }

  return dex_future_new_for_int (42);
}

static void
test_deferred_native_observer (gconstpointer data)
{
  DexFuture *thread;
  GError *error = NULL;
  const GValue *value;

  thread = dex_thread_spawn ("deferred-native-observer", native_observer, (gpointer)data, NULL);
  wait_for (thread);

  value = dex_future_get_value (thread, &error);
  g_assert_no_error (error);
  g_assert_nonnull (value);
  g_assert_cmpint (g_value_get_int (value), ==, 42);

  dex_unref (thread);
}

typedef struct
{
  DexPromise *started;
  DexPromise *release;
  DexPromise *destroyed;
} ThreadState;

static ThreadState *
thread_state_new (DexPromise *started,
                  DexPromise *release,
                  DexPromise *destroyed)
{
  ThreadState *state = g_new0 (ThreadState, 1);

  state->started = dex_ref (started);
  state->release = dex_ref (release);
  state->destroyed = dex_ref (destroyed);

  return state;
}

static void
thread_state_free (gpointer data)
{
  ThreadState *state = data;
  DexPromise *destroyed = g_steal_pointer (&state->destroyed);

  dex_clear (&state->started);
  dex_clear (&state->release);
  g_free (state);

  dex_promise_resolve_boolean (destroyed, TRUE);
  dex_unref (destroyed);
}

static DexFuture *
thread_wait_for_release (gpointer data)
{
  ThreadState *state = data;
  GError *error = NULL;

  dex_promise_resolve_boolean (state->started, TRUE);
  g_assert_true (dex_thread_wait_for (dex_ref (DEX_FUTURE (state->release)), &error));
  g_assert_no_error (error);

  /* The callback data must remain usable after cancellation. */
  g_assert_true (dex_future_is_pending (DEX_FUTURE (state->destroyed)));

  return dex_future_new_for_int (42);
}

static DexFuture *
thread_return_pending (gpointer data)
{
  ThreadState *state = data;

  dex_promise_resolve_boolean (state->started, TRUE);

  return dex_ref (DEX_FUTURE (state->release));
}

static void
test_deferred_cancel_running_thread (void)
{
  DexTaskGroup *group = dex_task_group_new (DEX_TASK_GROUP_FLAGS_NONE);
  DexPromise *started = dex_promise_new ();
  DexPromise *release = dex_promise_new ();
  DexPromise *destroyed = dex_promise_new ();
  DexDeferred *deferred;

  deferred = dex_deferred_new_thread ("deferred-cancel",
                                      thread_wait_for_release,
                                      thread_state_new (started, release, destroyed),
                                      thread_state_free);
  g_assert_true (dex_task_group_add (group, dex_ref (DEX_FUTURE (deferred))));
  wait_for (DEX_FUTURE (started));

  dex_task_group_cancel (group);
  g_assert_true (dex_future_is_rejected (DEX_FUTURE (deferred)));
  g_assert_true (dex_future_is_pending (DEX_FUTURE (destroyed)));

  dex_clear (&deferred);
  dex_clear (&group);
  g_assert_true (dex_future_is_pending (DEX_FUTURE (destroyed)));

  dex_promise_resolve_boolean (release, TRUE);
  wait_for (DEX_FUTURE (destroyed));

  dex_clear (&started);
  dex_clear (&release);
  dex_clear (&destroyed);
}

static void
test_deferred_thread_pool_lifetime (void)
{
  /* Bound failures if observing or discarding synchronously joins a worker
   * whose result still needs this thread to resolve it.
   */
  if (!g_test_subprocess ())
    {
      g_test_trap_subprocess (NULL, 5 * G_USEC_PER_SEC, 0);
      g_test_trap_assert_passed ();
      return;
    }

  for (guint discard = 0; discard < 2; discard++)
    {
      DexThreadPool *pool = dex_thread_pool_new (1);
      DexPromise *started = dex_promise_new ();
      DexPromise *release = dex_promise_new ();
      DexPromise *destroyed = dex_promise_new ();
      DexDeferred *deferred;
      DexFuture *observer;
      DexWeakRef pool_wr;
      gint64 deadline;

      dex_weak_ref_init (&pool_wr, pool);
      deferred = dex_deferred_new_on_thread_pool (pool,
                                                  "deferred-pool-lifetime",
                                                  thread_return_pending,
                                                  thread_state_new (started, release, destroyed),
                                                  thread_state_free);
      dex_clear (&pool);

      observer = dex_future_then (DEX_FUTURE (deferred), pass_through, NULL, NULL);
      wait_for (DEX_FUTURE (started));
      g_assert_true (dex_future_is_pending (observer));

      if (discard)
        dex_clear (&observer);

      g_assert_true (dex_future_is_pending (DEX_FUTURE (destroyed)));
      dex_promise_resolve_boolean (release, TRUE);

      if (observer != NULL)
        {
          wait_for (observer);
          g_assert_true (dex_future_is_resolved (observer));
          dex_clear (&observer);
        }

      wait_for (DEX_FUTURE (destroyed));

      /* Completion must eventually release the deferred's pool reference. */
      deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;
      while ((pool = dex_weak_ref_get (&pool_wr)))
        {
          dex_unref (pool);
          g_assert_cmpint (g_get_monotonic_time (), <, deadline);
          g_main_context_iteration (NULL, FALSE);
        }

      dex_weak_ref_clear (&pool_wr);
      dex_clear (&started);
      dex_clear (&release);
      dex_clear (&destroyed);
    }
}

static void
test_deferred_unused_cleanup (void)
{
  DexThreadPool *pool = dex_thread_pool_new (1);
  DexDeferred *deferred[5];
  int calls = 0;
  int destroyed = 0;

  deferred[0] = dex_deferred_new (NULL, return_value,
                                  state_new (&calls, &destroyed), state_free);
  deferred[1] = dex_deferred_new_fiber (NULL, 0, return_value,
                                        state_new (&calls, &destroyed), state_free);
  deferred[2] = dex_deferred_new_coroutine (NULL, coroutine_value,
                                            state_new (&calls, &destroyed), state_free);
  deferred[3] = dex_deferred_new_thread ("deferred-test", return_value,
                                         state_new (&calls, &destroyed), state_free);
  deferred[4] = dex_deferred_new_on_thread_pool (pool, "deferred-test", return_value,
                                                 state_new (&calls, &destroyed), state_free);

  for (guint i = 0; i < G_N_ELEMENTS (deferred); i++)
    dex_unref (deferred[i]);

  g_assert_cmpint (g_atomic_int_get (&calls), ==, 0);
  g_assert_cmpint (g_atomic_int_get (&destroyed), ==, 5);
  dex_unref (pool);
}

static void
test_deferred_await (void)
{
  int calls = 0;
  int destroyed = 0;
  DexDeferred *deferred = dex_deferred_new (NULL, return_value,
                                            state_new (&calls, &destroyed), state_free);
  DexFuture *fiber;
  const GValue *value;
  GError *error = NULL;

  fiber = dex_scheduler_spawn (NULL,
                               0,
                               await_deferred,
                               dex_ref (deferred),
                               dex_unref);
  g_assert_cmpint (g_atomic_int_get (&calls), ==, 0);

  wait_for (fiber);
  value = dex_future_get_value (fiber, &error);
  g_assert_no_error (error);
  g_assert_cmpint (g_value_get_int (value), ==, 42);
  g_assert_cmpint (g_atomic_int_get (&calls), ==, 1);

  dex_unref (fiber);
  dex_unref (deferred);
}

static void
test_deferred_delayed_result (void)
{
  DexPromise *promise = dex_promise_new ();
  DexDeferred *deferred = dex_deferred_new (NULL,
                                            return_promise,
                                            dex_ref (promise),
                                            dex_unref);
  DexPromise *dependent = dex_promise_new ();
  GError *error = NULL;

  dex_future_chain (DEX_FUTURE (deferred), DEX_FUTURE (dependent));
  g_assert_true (dex_future_is_pending (DEX_FUTURE (deferred)));
  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);
  g_assert_true (dex_future_is_pending (DEX_FUTURE (deferred)));

  dex_promise_reject (promise,
                      g_error_new_literal (DEX_ERROR,
                                           DEX_ERROR_UNKNOWN,
                                           "deferred failure"));
  wait_for (DEX_FUTURE (dependent));
  g_assert_null (dex_future_get_value (DEX_FUTURE (deferred), &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_UNKNOWN);
  g_clear_error (&error);

  dex_unref (dependent);
  dex_unref (deferred);
  dex_unref (promise);
}

typedef struct
{
  DexDeferred *deferred;
  DexFuture   *dependent;
} DiscardState;

static gpointer
observe_deferred (gpointer data)
{
  DiscardState *state = data;

  dex_future_chain (DEX_FUTURE (state->deferred), state->dependent);

  return NULL;
}

static void
test_deferred_concurrent_observers (void)
{
  int calls = 0;
  int destroyed = 0;

  for (guint i = 0; i < 100; i++)
    {
      DexDeferred *deferred = dex_deferred_new (NULL, return_value,
                                                state_new (&calls, &destroyed), state_free);
      DexPromise *first = dex_promise_new ();
      DexPromise *second = dex_promise_new ();
      DiscardState first_state = { deferred, DEX_FUTURE (first) };
      DiscardState second_state = { deferred, DEX_FUTURE (second) };
      GThread *first_thread = g_thread_new ("deferred-observe", observe_deferred, &first_state);
      GThread *second_thread = g_thread_new ("deferred-observe", observe_deferred, &second_state);

      g_thread_join (first_thread);
      g_thread_join (second_thread);
      wait_for (DEX_FUTURE (first));
      wait_for (DEX_FUTURE (second));
      g_assert_cmpint (g_atomic_int_get (&calls), ==, i + 1);

      dex_unref (first);
      dex_unref (second);
      dex_unref (deferred);
    }

  g_assert_cmpint (g_atomic_int_get (&destroyed), ==, 100);
}

static gpointer
discard_deferred (gpointer data)
{
  DiscardState *state = data;

  dex_future_discard (DEX_FUTURE (state->deferred), state->dependent);

  return NULL;
}

static void
test_deferred_discard_before_callback (void)
{
  int calls = 0;
  int destroyed = 0;
  DexDeferred *deferred = dex_deferred_new (NULL, return_value,
                                            state_new (&calls, &destroyed), state_free);
  DexPromise *dependent = dex_promise_new ();
  DexPromise *late = dex_promise_new ();
  GError *error = NULL;

  dex_future_chain (DEX_FUTURE (deferred), DEX_FUTURE (dependent));
  dex_future_discard (DEX_FUTURE (deferred), DEX_FUTURE (dependent));
  wait_for (DEX_FUTURE (deferred));

  g_assert_null (dex_future_get_value (DEX_FUTURE (deferred), &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_assert_cmpint (g_atomic_int_get (&calls), ==, 0);
  g_assert_cmpint (g_atomic_int_get (&destroyed), ==, 1);

  dex_future_chain (DEX_FUTURE (deferred), DEX_FUTURE (late));
  g_assert_true (dex_future_is_rejected (DEX_FUTURE (late)));

  g_clear_error (&error);
  dex_unref (late);
  dex_unref (dependent);
  dex_unref (deferred);
}

static void
test_deferred_discard_after_callback (void)
{
  DexPromise *promise = dex_promise_new_cancellable ();
  DexDeferred *deferred = dex_deferred_new (NULL,
                                            return_promise,
                                            dex_ref (promise),
                                            dex_unref);
  DexPromise *dependent = dex_promise_new ();
  gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

  dex_future_chain (DEX_FUTURE (deferred), DEX_FUTURE (dependent));
  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);
  dex_future_discard (DEX_FUTURE (deferred), DEX_FUTURE (dependent));

  while (!g_cancellable_is_cancelled (dex_promise_get_cancellable (promise)) &&
         g_get_monotonic_time () < deadline)
    g_main_context_iteration (NULL, FALSE);

  g_assert_true (g_cancellable_is_cancelled (dex_promise_get_cancellable (promise)));

  dex_unref (dependent);
  dex_unref (deferred);
  dex_unref (promise);
}

static void
test_deferred_concurrent_discard (void)
{
  for (guint i = 0; i < 200; i++)
    {
      DexDeferred *deferred = dex_deferred_new (NULL,
                                                return_promise,
                                                dex_future_new_for_int (42),
                                                dex_unref);
      DexPromise *dependent = dex_promise_new ();
      DiscardState state = { deferred, DEX_FUTURE (dependent) };
      GThread *thread;

      dex_future_chain (DEX_FUTURE (deferred), DEX_FUTURE (dependent));
      thread = g_thread_new ("deferred-discard", discard_deferred, &state);
      g_thread_join (thread);
      wait_for (DEX_FUTURE (deferred));

      dex_unref (dependent);
      dex_unref (deferred);
    }
}

int
main (int   argc,
      char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Dex/Deferred/kinds", test_deferred_kinds);
  g_test_add_func ("/Dex/Deferred/closed_thread_pool", test_deferred_closed_thread_pool);
  g_test_add_func ("/Dex/Deferred/no_result", test_deferred_no_result);
  g_test_add_data_func ("/Dex/Deferred/native_observer", GINT_TO_POINTER (FALSE), test_deferred_native_observer);
  g_test_add_data_func ("/Dex/Deferred/native_observer_with_storage", GINT_TO_POINTER (TRUE), test_deferred_native_observer);
  g_test_add_func ("/Dex/Deferred/cancel_running_thread", test_deferred_cancel_running_thread);
  g_test_add_func ("/Dex/Deferred/thread_pool_lifetime", test_deferred_thread_pool_lifetime);
  g_test_add_func ("/Dex/Deferred/unused_cleanup", test_deferred_unused_cleanup);
  g_test_add_func ("/Dex/Deferred/await", test_deferred_await);
  g_test_add_func ("/Dex/Deferred/delayed_result", test_deferred_delayed_result);
  g_test_add_func ("/Dex/Deferred/concurrent_observers", test_deferred_concurrent_observers);
  g_test_add_func ("/Dex/Deferred/discard_before_callback", test_deferred_discard_before_callback);
  g_test_add_func ("/Dex/Deferred/discard_after_callback", test_deferred_discard_after_callback);
  g_test_add_func ("/Dex/Deferred/concurrent_discard", test_deferred_concurrent_discard);
  return g_test_run ();
}
