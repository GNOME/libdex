/* test-scheduler.c
 *
 * Copyright 2022 Christian Hergert <christian@sourceandstack.com>
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

#include <libdex.h>

#include "dex-thread-pool-scheduler-private.h"

typedef struct _DirectedSteal DirectedSteal;

typedef struct _DirectedStealItem
{
  DirectedSteal *state;
  gboolean       completed;
} DirectedStealItem;

struct _DirectedSteal
{
  GMutex             mutex;
  GCond              cond;
  DexScheduler      *scheduler;
  GThread           *producer_thread;
  DirectedStealItem *items;
  guint              n_items;
  guint              n_completed;
  guint              n_completed_by_peer;
  gboolean           producer_finished;
  gboolean           peer_timed_out;
};

typedef struct _MixedLocalSteal MixedLocalSteal;

typedef struct _MixedLocalStealItem
{
  MixedLocalSteal *state;
  gboolean         completed;
} MixedLocalStealItem;

typedef struct _MixedLocalStealWorker
{
  MixedLocalSteal *state;
  guint            index;
} MixedLocalStealWorker;

struct _MixedLocalSteal
{
  GMutex                 mutex;
  GCond                  cond;
  DexScheduler          *scheduler;
  GThread               *producer_thread;
  GThread               *thief_thread;
  GPtrArray             *fibers;
  MixedLocalStealItem   *items;
  MixedLocalStealWorker *workers;
  guint                  n_workers;
  guint                  n_ready;
  guint                  n_finished;
  guint                  n_local_completed;
  guint                  n_batch_items;
  guint                  n_batch_completed;
  guint                  n_batch_completed_by_peer;
  guint                  n_sentinel_completed_by_thief;
  gboolean               producer_go;
  gboolean               producer_published;
  gboolean               thief_go;
  gboolean               release_all;
};

static DexScheduler *thread_pool;
static GMainLoop *main_loop;

static void
test_main_scheduler_simple_cb (gpointer data)
{
  gboolean *count = data;
  *count = 123;
  g_main_loop_quit (main_loop);
}

static void
test_main_scheduler_simple (void)
{
  DexScheduler *scheduler = dex_scheduler_get_default ();
  gboolean count = 0;

  g_assert_nonnull (scheduler);
  g_assert_true (DEX_IS_MAIN_SCHEDULER (scheduler));

  main_loop = g_main_loop_new (NULL, FALSE);
  dex_scheduler_push (scheduler,
                      test_main_scheduler_simple_cb,
                      &count);
  g_main_loop_run (main_loop);
  g_clear_pointer (&main_loop, g_main_loop_unref);

  g_assert_cmpint (count, ==, 123);
}

static DexFuture *
test_fiber2_func (gpointer user_data)
{
  guint *count = user_data;
  g_atomic_int_inc (count);
  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
test_fiber_func (gpointer user_data)
{
  GPtrArray *all = g_ptr_array_new_with_free_func (dex_unref);

  for (guint i = 0; i < 10; i++)
    g_ptr_array_add (all, dex_scheduler_spawn (dex_scheduler_get_thread_default (),
                                               dex_get_min_stack_size (),
                                               test_fiber2_func, user_data, NULL));

  dex_await (dex_future_allv ((DexFuture **)all->pdata, all->len), NULL);

  g_ptr_array_unref (all);

  return NULL;
}

static DexFuture *
spawner (gpointer user_data)
{
  GPtrArray *all = g_ptr_array_new_with_free_func (dex_unref);

  for (guint i = 0; i < 1000; i++)
    g_ptr_array_add (all, dex_scheduler_spawn (thread_pool,
                                               dex_get_min_stack_size (),
                                               test_fiber_func, user_data, NULL));

  dex_await (dex_future_allv ((DexFuture **)(gpointer)all->pdata, all->len), NULL);

  g_ptr_array_unref (all);

  return NULL;
}

static DexFuture *
named_fiber_func (gpointer user_data)
{
  return dex_future_new_for_boolean (TRUE);
}

static void
test_scheduler_spawn_static_name (void)
{
  DexFuture * future = NULL;

  future = dex_scheduler_spawn (NULL, 0, named_fiber_func, NULL, NULL);

  g_assert_cmpstr (dex_future_get_name (future), ==, "named_fiber_func");

  while (dex_future_get_status (future) == DEX_FUTURE_STATUS_PENDING)
    g_main_context_iteration (NULL, TRUE);

  dex_clear (&future);
}

static DexFuture *
quit_cb (DexFuture *completed,
         gpointer   user_data)
{
  g_test_message ("Quiting main loop");
  g_main_loop_quit (main_loop);
  return NULL;
}

static void
test_thread_pool_scheduler_spawn (void)
{
  DexFuture *future;
  guint count = 0;

  thread_pool = dex_thread_pool_scheduler_new ();
  main_loop = g_main_loop_new (NULL, FALSE);

  g_test_message ("Spawning with stack size %u",
                  (guint)dex_get_min_stack_size ());

  future = dex_scheduler_spawn (NULL, 0, spawner, &count, NULL);
  future = dex_future_finally (future, quit_cb, NULL, NULL);

  g_test_message ("Running main loop");
  g_main_loop_run (main_loop);

  g_assert_cmpint (count, ==, 10*1000);

  dex_unref (future);
  dex_unref (thread_pool);
}

static void
test_thread_pool_scheduler_push_cb (gpointer data)
{
  struct {
    GMutex mutex;
    GCond cond;
  } *syncobj = data;

  g_mutex_lock (&syncobj->mutex);
  g_cond_signal (&syncobj->cond);
  g_mutex_unlock (&syncobj->mutex);
}

static void
test_thread_pool_scheduler_push (void)
{
  struct {
    GMutex mutex;
    GCond cond;
  } syncobj;

  g_mutex_init (&syncobj.mutex);
  g_cond_init (&syncobj.cond);

  g_mutex_lock (&syncobj.mutex);
  dex_scheduler_push (dex_thread_pool_scheduler_get_default (),
                      test_thread_pool_scheduler_push_cb,
                      &syncobj);
  g_cond_wait (&syncobj.cond, &syncobj.mutex);
  g_mutex_unlock (&syncobj.mutex);

  g_mutex_clear (&syncobj.mutex);
  g_cond_clear (&syncobj.cond);
}

static void
test_thread_pool_scheduler_directed_steal_item (gpointer data)
{
  DirectedStealItem *item = data;
  DirectedSteal *state = item->state;

  g_mutex_lock (&state->mutex);

  g_assert_false (item->completed);
  item->completed = TRUE;
  state->n_completed++;

  if (g_thread_self () != state->producer_thread)
    state->n_completed_by_peer++;

  g_cond_broadcast (&state->cond);
  g_mutex_unlock (&state->mutex);
}

static void
test_thread_pool_scheduler_directed_steal_producer (gpointer data)
{
  DirectedSteal *state = data;
  gint64 deadline;

  g_mutex_lock (&state->mutex);
  state->producer_thread = g_thread_self ();
  g_mutex_unlock (&state->mutex);

  for (guint i = 0; i < state->n_items; i++)
    {
      state->items[i].state = state;
      dex_scheduler_push (state->scheduler,
                          test_thread_pool_scheduler_directed_steal_item,
                          &state->items[i]);
    }

  deadline = g_get_monotonic_time () + (5 * G_TIME_SPAN_SECOND);

  g_mutex_lock (&state->mutex);

  while (state->n_completed_by_peer == 0)
    {
      if (!g_cond_wait_until (&state->cond, &state->mutex, deadline))
        {
          state->peer_timed_out = TRUE;
          break;
        }
    }

  state->producer_finished = TRUE;
  g_cond_broadcast (&state->cond);
  g_mutex_unlock (&state->mutex);
}

static void
test_thread_pool_scheduler_directed_steal (void)
{
  DirectedSteal state;
  gint64 deadline;

  state = (DirectedSteal) {
    .scheduler = dex_thread_pool_scheduler_new (),
    .n_items = 128,
  };

  if (_dex_thread_pool_scheduler_get_n_workers (state.scheduler) < 2)
    {
      g_test_skip ("At least two thread-pool workers are required");
      dex_clear (&state.scheduler);
      return;
    }

  g_mutex_init (&state.mutex);
  g_cond_init (&state.cond);
  state.items = g_new0 (DirectedStealItem, state.n_items);

  dex_scheduler_push (state.scheduler,
                      test_thread_pool_scheduler_directed_steal_producer,
                      &state);

  deadline = g_get_monotonic_time () + (10 * G_TIME_SPAN_SECOND);

  g_mutex_lock (&state.mutex);

  while (!state.producer_finished || state.n_completed < state.n_items)
    {
      if (!g_cond_wait_until (&state.cond, &state.mutex, deadline))
        g_error ("Timed out waiting for directed thread-pool work");
    }

  g_mutex_unlock (&state.mutex);

  g_assert_false (state.peer_timed_out);
  g_assert_cmpuint (state.n_completed_by_peer, >, 0);
  g_assert_cmpuint (state.n_completed, ==, state.n_items);

  dex_clear (&state.scheduler);
  g_clear_pointer (&state.items, g_free);
  g_mutex_clear (&state.mutex);
  g_cond_clear (&state.cond);
}

static void
test_thread_pool_scheduler_mixed_local_item (gpointer data)
{
  MixedLocalStealWorker *worker = data;
  MixedLocalSteal *state = worker->state;

  g_mutex_lock (&state->mutex);
  state->n_local_completed++;

  if (worker->index == 0 && g_thread_self () == state->thief_thread)
    state->n_sentinel_completed_by_thief++;

  g_cond_broadcast (&state->cond);
  g_mutex_unlock (&state->mutex);
}

static void
test_thread_pool_scheduler_mixed_batch_item (gpointer data)
{
  MixedLocalStealItem *item = data;
  MixedLocalSteal *state = item->state;

  g_mutex_lock (&state->mutex);

  g_assert_false (item->completed);
  item->completed = TRUE;
  state->n_batch_completed++;

  if (g_thread_self () != state->producer_thread)
    state->n_batch_completed_by_peer++;

  g_cond_broadcast (&state->cond);
  g_mutex_unlock (&state->mutex);
}

static DexFuture *
test_thread_pool_scheduler_mixed_worker (gpointer data)
{
  MixedLocalStealWorker *worker = data;
  MixedLocalSteal *state = worker->state;

  g_mutex_lock (&state->mutex);

  if (worker->index == 1)
    state->producer_thread = g_thread_self ();
  else if (worker->index == state->n_workers - 1)
    state->thief_thread = g_thread_self ();

  g_mutex_unlock (&state->mutex);

  dex_scheduler_push (state->scheduler,
                      test_thread_pool_scheduler_mixed_local_item,
                      worker);

  g_mutex_lock (&state->mutex);
  state->n_ready++;
  g_cond_broadcast (&state->cond);

  if (worker->index == 1)
    {
      while (!state->producer_go)
        g_cond_wait (&state->cond, &state->mutex);

      g_mutex_unlock (&state->mutex);

      for (guint i = 0; i < state->n_batch_items; i++)
        {
          state->items[i].state = state;
          dex_scheduler_push (state->scheduler,
                              test_thread_pool_scheduler_mixed_batch_item,
                              &state->items[i]);
        }

      g_mutex_lock (&state->mutex);
      state->producer_published = TRUE;
      g_cond_broadcast (&state->cond);
    }
  else if (worker->index == state->n_workers - 1)
    {
      while (!state->thief_go)
        g_cond_wait (&state->cond, &state->mutex);
    }

  if (worker->index != state->n_workers - 1)
    {
      while (!state->release_all)
        g_cond_wait (&state->cond, &state->mutex);
    }

  state->n_finished++;
  g_cond_broadcast (&state->cond);
  g_mutex_unlock (&state->mutex);

  return dex_future_new_for_boolean (TRUE);
}

static void
test_thread_pool_scheduler_mixed_local_steal (void)
{
  MixedLocalSteal state;
  gint64 deadline;
  gboolean peer_timed_out = FALSE;

  state = (MixedLocalSteal) {
    .scheduler = dex_thread_pool_scheduler_new (),
  };
  state.n_workers = _dex_thread_pool_scheduler_get_n_workers (state.scheduler);

  if (state.n_workers < 3)
    {
      g_test_skip ("At least three thread-pool workers are required");
      dex_clear (&state.scheduler);
      return;
    }

  state.n_batch_items = MAX (128, state.n_workers * 2);
  state.fibers = g_ptr_array_new_with_free_func (dex_unref);
  state.items = g_new0 (MixedLocalStealItem, state.n_batch_items);
  state.workers = g_new0 (MixedLocalStealWorker, state.n_workers);
  g_mutex_init (&state.mutex);
  g_cond_init (&state.cond);

  for (guint i = 0; i < state.n_workers; i++)
    {
      state.workers[i].state = &state;
      state.workers[i].index = i;
      g_ptr_array_add (state.fibers,
                       dex_scheduler_spawn (state.scheduler,
                                            dex_get_min_stack_size (),
                                            test_thread_pool_scheduler_mixed_worker,
                                            &state.workers[i],
                                            NULL));
    }

  deadline = g_get_monotonic_time () + (10 * G_TIME_SPAN_SECOND);

  g_mutex_lock (&state.mutex);

  while (state.n_ready < state.n_workers)
    {
      if (!g_cond_wait_until (&state.cond, &state.mutex, deadline))
        g_error ("Timed out preparing mixed local thread-pool work");
    }

  state.producer_go = TRUE;
  g_cond_broadcast (&state.cond);

  while (!state.producer_published)
    {
      if (!g_cond_wait_until (&state.cond, &state.mutex, deadline))
        g_error ("Timed out publishing mixed local thread-pool work");
    }

  state.thief_go = TRUE;
  g_cond_broadcast (&state.cond);

  while (state.n_batch_completed_by_peer == 0)
    {
      if (!g_cond_wait_until (&state.cond, &state.mutex, deadline))
        {
          peer_timed_out = TRUE;
          break;
        }
    }

  state.release_all = TRUE;
  g_cond_broadcast (&state.cond);

  deadline = g_get_monotonic_time () + (10 * G_TIME_SPAN_SECOND);

  while (state.n_finished < state.n_workers ||
         state.n_local_completed < state.n_workers ||
         state.n_batch_completed < state.n_batch_items)
    {
      if (!g_cond_wait_until (&state.cond, &state.mutex, deadline))
        g_error ("Timed out draining mixed local thread-pool work");
    }

  g_test_message ("Mixed local steal: workers=%u local=%u batch=%u peer=%u sentinel=%u",
                  state.n_workers,
                  state.n_local_completed,
                  state.n_batch_completed,
                  state.n_batch_completed_by_peer,
                  state.n_sentinel_completed_by_thief);

  g_assert_false (peer_timed_out);
  g_assert_cmpuint (state.n_sentinel_completed_by_thief, ==, 1);
  g_assert_cmpuint (state.n_batch_completed_by_peer, >, 0);
  g_assert_cmpuint (state.n_batch_completed, ==, state.n_batch_items);

  g_mutex_unlock (&state.mutex);

  g_clear_pointer (&state.fibers, g_ptr_array_unref);
  dex_clear (&state.scheduler);
  g_clear_pointer (&state.items, g_free);
  g_clear_pointer (&state.workers, g_free);
  g_mutex_clear (&state.mutex);
  g_cond_clear (&state.cond);
}

int
main (int   argc,
      char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Dex/TestSuite/MainScheduler/simple", test_main_scheduler_simple);
  g_test_add_func ("/Dex/TestSuite/Scheduler/spawn_static_name", test_scheduler_spawn_static_name);
  g_test_add_func ("/Dex/TestSuite/ThreadPoolScheduler/10_000_fibers", test_thread_pool_scheduler_spawn);
  g_test_add_func ("/Dex/TestSuite/ThreadPoolScheduler/push", test_thread_pool_scheduler_push);
  g_test_add_func ("/Dex/TestSuite/ThreadPoolScheduler/directed_steal",
                   test_thread_pool_scheduler_directed_steal);
  g_test_add_func ("/Dex/TestSuite/ThreadPoolScheduler/mixed_local_steal",
                   test_thread_pool_scheduler_mixed_local_steal);
  return g_test_run ();
}
