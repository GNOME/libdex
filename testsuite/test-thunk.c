/* test-thunk.c
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <libdex.h>

#include "dex-block-private.h"
#include "dex-thunk-private.h"

typedef struct
{
  GThread    *expected_thread;
  DexFuture  *completion;
  DexThunk   *owner;
  DexPromise *release;
  GMutex      mutex;
  GCond       cond;
  gboolean    running;
  gboolean    proceed;
  int         called;
  int         destroyed;
} State;

static void
state_destroy (gpointer data)
{
  State *state = data;

  g_assert_true (g_thread_self () == state->expected_thread);
  if (state->completion != NULL)
    g_assert_true (dex_future_is_pending (state->completion));
  g_atomic_int_inc (&state->destroyed);
}

static DexFuture *
invoke (gpointer data)
{
  State *state = data;

  g_atomic_int_inc (&state->called);
  return dex_future_new_for_int (42);
}

static DexFuture *
invoke_callback (DexFuture *completed,
                 gpointer   data)
{
  return invoke (data);
}

static DexFuture *
invoke_coroutine (DexCoroutineContext *context,
                  gpointer             data)
{
  return invoke (data);
}

static void
wait_destroyed (State *state)
{
  gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

  while (g_atomic_int_get (&state->destroyed) == 0 && g_get_monotonic_time () < deadline)
    {
      while (g_main_context_pending (NULL))
        g_main_context_iteration (NULL, FALSE);
      g_usleep (100);
    }
  g_assert_cmpint (g_atomic_int_get (&state->destroyed), ==, 1);
}

static gpointer
release_thunk (gpointer data)
{
  dex_thunk_unref (data);
  return NULL;
}

static gpointer
release_object (gpointer data)
{
  dex_unref (data);
  return NULL;
}

static void
test_direct (void)
{
  State state = { .expected_thread = g_thread_self () };
  DexThunk *thunk = dex_thunk_new (G_CALLBACK (invoke), &state, state_destroy, NULL);
  DexFuture *completion = dex_thunk_dup_completion (thunk);
  DexFuture *again = dex_thunk_dup_completion (thunk);

  state.completion = completion;
  g_assert_true (completion == again);
  g_assert_cmpint (dex_await_int (dex_thunk_invoke (thunk), NULL), ==, 42);
  dex_thunk_ref (thunk);
  dex_thunk_unref (thunk);
  g_assert_cmpint (state.destroyed, ==, 0);
  dex_thunk_unref (thunk);
  g_assert_cmpint (state.destroyed, ==, 1);
  g_assert_true (dex_await_boolean (completion, NULL));
  dex_unref (again);
}

static DexFuture *
release_during_invoke (gpointer data)
{
  State *state = data;

  g_clear_pointer (&state->owner, dex_thunk_unref);
  g_assert_cmpint (state->destroyed, ==, 0);
  return dex_future_new_for_int (42);
}

static void
test_invocation_lease (void)
{
  State state = { .expected_thread = g_thread_self () };

  state.owner = dex_thunk_new (G_CALLBACK (release_during_invoke), &state, state_destroy, NULL);
  g_assert_cmpint (dex_await_int (dex_thunk_invoke (state.owner), NULL), ==, 42);
  g_assert_null (state.owner);
  g_assert_cmpint (state.destroyed, ==, 1);
}

static void
test_scheduled (void)
{
  for (guint foreign = 0; foreign < 2; foreign++)
    {
      State state = { .expected_thread = g_thread_self () };
      DexThunk *thunk = dex_thunk_new (G_CALLBACK (invoke),
                                       &state,
                                       state_destroy,
                                       dex_scheduler_get_default ());
      DexFuture *completion = dex_thunk_dup_completion (thunk);

      state.completion = completion;
      if (foreign)
        g_thread_join (g_thread_new ("thunk-release", release_thunk, thunk));
      else
        dex_thunk_unref (thunk);
      g_assert_cmpint (state.destroyed, ==, 0);
      g_assert_true (dex_future_is_pending (completion));
      wait_destroyed (&state);
      g_assert_true (dex_await_boolean (completion, NULL));
      g_assert_cmpint (state.called, ==, 0);
    }
}

static void
test_concurrent_release (void)
{
  State state = { .expected_thread = g_thread_self () };
  DexThunk *thunk = dex_thunk_new (G_CALLBACK (invoke),
                                   &state,
                                   state_destroy,
                                   dex_scheduler_get_default ());
  GThread *threads[16];

  for (guint i = 0; i < G_N_ELEMENTS (threads); i++)
    threads[i] = g_thread_new ("thunk-release", release_thunk, dex_thunk_ref (thunk));
  for (guint i = 0; i < G_N_ELEMENTS (threads); i++)
    g_thread_join (threads[i]);
  g_assert_cmpint (state.destroyed, ==, 0);
  dex_thunk_unref (thunk);
  wait_destroyed (&state);
}

static void
test_block_release (void)
{
  for (guint mode = 0; mode < 3; mode++)
    {
      State state = { .expected_thread = g_thread_self () };
      DexPromise *promise = dex_promise_new ();
      DexFuture *block = dex_block_new (dex_ref (promise),
                                        dex_scheduler_get_default (),
                                        DEX_BLOCK_KIND_THEN,
                                        invoke_callback,
                                        &state,
                                        state_destroy);

      if (mode == 1)
        dex_promise_reject (promise,
                            g_error_new_literal (G_IO_ERROR,
                                                 G_IO_ERROR_FAILED,
                                                 "skip"));
      else if (mode == 2)
        dex_promise_resolve_boolean (promise, TRUE);

      if (mode != 0)
        while (dex_future_is_pending (block))
          g_main_context_iteration (NULL, TRUE);
      g_thread_join (g_thread_new ("block-release", release_object, block));
      wait_destroyed (&state);
      g_assert_cmpint (state.called, ==, mode == 2);
      dex_unref (promise);
    }
}

static void
test_unused_deferred (void)
{
  DexThreadPool *pool = dex_thread_pool_new (1);

  for (guint kind = 0; kind < 5; kind++)
    {
      State state = { .expected_thread = g_thread_self () };
      DexDeferred *deferred;

      switch (kind)
        {
        case 0:
          deferred = dex_deferred_new (NULL, invoke, &state, state_destroy);
          break;

        case 1:
          deferred = dex_deferred_new_fiber (NULL, 0, invoke, &state, state_destroy);
          break;

        case 2:
          deferred = dex_deferred_new_coroutine (NULL, invoke_coroutine, &state, state_destroy);
          break;

        case 3:
          deferred = dex_deferred_new_thread (NULL, invoke, &state, state_destroy);
          break;

        case 4:
          deferred = dex_deferred_new_on_thread_pool (pool, NULL, invoke, &state, state_destroy);
          break;

        default:
          g_assert_not_reached ();
        }
      g_thread_join (g_thread_new ("deferred-release", release_object, deferred));
      g_assert_cmpint (state.destroyed, ==, 0);
      wait_destroyed (&state);
      g_assert_cmpint (state.called, ==, 0);
    }

  dex_unref (pool);
}

static DexFuture *
thread_work (gpointer data)
{
  State *state = data;

  g_mutex_lock (&state->mutex);
  state->running = TRUE;
  g_cond_signal (&state->cond);
  while (!state->proceed)
    g_cond_wait (&state->cond, &state->mutex);
  g_assert_cmpint (g_atomic_int_get (&state->destroyed), ==, 0);
  g_mutex_unlock (&state->mutex);
  return dex_ref (state->release);
}

static void
test_abandoned_thread (void)
{
  State state = { .expected_thread = g_thread_self () };
  DexFuture *future;

  g_mutex_init (&state.mutex);
  g_cond_init (&state.cond);
  state.release = dex_promise_new ();
  future = dex_thread_spawn ("thunk-worker", thread_work, &state, state_destroy);
  g_mutex_lock (&state.mutex);
  while (!state.running)
    g_cond_wait (&state.cond, &state.mutex);
  g_mutex_unlock (&state.mutex);
  dex_unref (future);
  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);
  g_assert_cmpint (state.destroyed, ==, 0);
  g_mutex_lock (&state.mutex);
  state.proceed = TRUE;
  g_cond_signal (&state.cond);
  g_mutex_unlock (&state.mutex);
  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);
  g_assert_cmpint (state.destroyed, ==, 0);
  dex_promise_resolve_boolean (state.release, TRUE);
  wait_destroyed (&state);
  dex_unref (state.release);
  g_cond_clear (&state.cond);
  g_mutex_clear (&state.mutex);
}

static DexFuture *
pool_work (gpointer data)
{
  State *state = data;

  g_assert_true (g_thread_self () != state->expected_thread);
  return invoke (data);
}

static void
test_pool_cleanup (void)
{
  for (guint deferred = 0; deferred < 2; deferred++)
    for (guint closed = 0; closed < 2; closed++)
      {
        State state = { .expected_thread = g_thread_self () };
        DexThreadPool *pool = dex_thread_pool_new (1);

        DexFuture *future;
        GError *error = NULL;

        if (closed)
          dex_future_disown (dex_thread_pool_close (pool, DEX_THREAD_POOL_SHUTDOWN_DRAIN));

        if (deferred)
          future = DEX_FUTURE (dex_deferred_new_on_thread_pool (pool, NULL,
                                                               pool_work, &state, state_destroy));
        else
          future = dex_thread_pool_submit (pool, NULL, pool_work, &state, state_destroy);

        dex_future_disown (dex_ref (future));
        wait_destroyed (&state);
        if (closed)
          {
            g_assert_null (dex_future_get_value (future, &error));
            g_assert_error (error, DEX_ERROR, DEX_ERROR_SEMAPHORE_CLOSED);
            g_assert_cmpint (state.called, ==, 0);
            g_clear_error (&error);
          }
        else
          {
            g_assert_cmpint (dex_await_int (dex_ref (future), &error), ==, 42);
            g_assert_no_error (error);
            g_assert_cmpint (state.called, ==, 1);
          }
        dex_unref (future);
        dex_unref (pool);
      }
}

static gpointer
close_pool (gpointer data)
{
  return dex_thread_pool_close (data, DEX_THREAD_POOL_SHUTDOWN_CANCEL_QUEUED);
}

static void
test_pool_cancel_cleanup (void)
{
  State running = { .expected_thread = g_thread_self () };
  State queued = { .expected_thread = g_thread_self () };
  DexThreadPool *pool = dex_thread_pool_new (1);
  DexFuture *active;
  DexFuture *pending;
  DexFuture *close;
  GError *error = NULL;

  g_mutex_init (&running.mutex);
  g_cond_init (&running.cond);
  running.release = dex_promise_new ();
  active = dex_thread_pool_submit (pool, NULL, thread_work, &running, state_destroy);
  g_mutex_lock (&running.mutex);
  while (!running.running)
    g_cond_wait (&running.cond, &running.mutex);
  g_mutex_unlock (&running.mutex);

  pending = dex_thread_pool_submit (pool, NULL, pool_work, &queued, state_destroy);
  close = g_thread_join (g_thread_new ("pool-close", close_pool, pool));
  g_assert_cmpint (queued.destroyed, ==, 0);
  wait_destroyed (&queued);
  g_assert_cmpint (queued.called, ==, 0);
  g_assert_null (dex_future_get_value (pending, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_SEMAPHORE_CLOSED);
  g_clear_error (&error);

  g_mutex_lock (&running.mutex);
  running.proceed = TRUE;
  g_cond_signal (&running.cond);
  g_mutex_unlock (&running.mutex);
  dex_promise_resolve_boolean (running.release, TRUE);
  wait_destroyed (&running);
  dex_unref (active);
  dex_unref (pending);
  dex_unref (close);
  dex_unref (pool);
  dex_unref (running.release);
  g_cond_clear (&running.cond);
  g_mutex_clear (&running.mutex);
}

int
main (int argc, char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/thunk/direct", test_direct);
  g_test_add_func ("/thunk/invocation-lease", test_invocation_lease);
  g_test_add_func ("/thunk/scheduled", test_scheduled);
  g_test_add_func ("/thunk/concurrent-release", test_concurrent_release);
  g_test_add_func ("/thunk/block-release", test_block_release);
  g_test_add_func ("/thunk/unused-deferred", test_unused_deferred);
  g_test_add_func ("/thunk/abandoned-thread", test_abandoned_thread);
  g_test_add_func ("/thunk/pool-cleanup", test_pool_cleanup);
  g_test_add_func ("/thunk/pool-cancel-cleanup", test_pool_cancel_cleanup);
  return g_test_run ();
}
