/*
 * dex-thread-pool-scheduler.c
 *
 * Copyright 2022 Christian Hergert <christian@sourceandstack.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifdef __linux__
# ifndef _GNU_SOURCE
#  define _GNU_SOURCE
# endif
#endif

#include "config.h"

#include <stdatomic.h>

#ifdef __linux__
# include <errno.h>
# include <sched.h>
#endif

#include "dex-scheduler-private.h"
#include "dex-thread-pool-scheduler-private.h"
#include "dex-thread-pool-worker-private.h"
#include "dex-thread-storage-private.h"
#include "dex-work-queue-private.h"

#define MAX_WORKERS 32

/**
 * DexThreadPoolScheduler:
 *
 * `DexThreadPoolScheduler` is a [class@Dex.Scheduler] that will dispatch work
 * items and fibers to sub-schedulers on a specific operating system thread.
 *
 * [class@Dex.Fiber] will never migrate from the thread they are created on to
 * reduce chances of safety issues involved in tracking state between CPU.
 *
 * New work items are placed into a global work queue and then dispatched
 * efficiently to a single thread pool worker using a specialized async
 * semaphore. On modern Linux using io_uring, this wakes up a single worker
 * thread and therefore is not subject to "thundering herd" common with
 * global work queues.
 *
 * When a worker creates a new work item, it is placed into a work stealing
 * queue owned by the thread. Other worker threads may steal work items when
 * they have exhausted their own work queue.
 */

struct _DexThreadPoolScheduler
{
  DexScheduler            parent_instance;
  DexWorkQueue           *global_work_queue;
  DexThreadPoolWorkerSet *set;
  guint                   fiber_rrobin;
  guint                   n_workers;
  DexThreadPoolWorker    *workers[MAX_WORKERS];
};

typedef struct _DexThreadPoolSchedulerClass
{
  DexSchedulerClass parent_class;
} DexThreadPoolSchedulerClass;

DEX_DEFINE_FINAL_TYPE (DexThreadPoolScheduler, dex_thread_pool_scheduler, DEX_TYPE_SCHEDULER)

#undef DEX_TYPE_THREAD_POOL_SCHEDULER
#define DEX_TYPE_THREAD_POOL_SCHEDULER dex_thread_pool_scheduler_type

static DexScheduler *default_thread_pool;

#ifdef __linux__
static char *
dex_thread_pool_scheduler_get_core_siblings (guint cpu)
{
  char *path;
  char *contents = NULL;

  path = g_strdup_printf ("/sys/devices/system/cpu/cpu%u/topology/thread_siblings_list", cpu);

  if (!g_file_get_contents (path, &contents, NULL, NULL))
    {
      g_free (path);
      g_free (contents);
      return NULL;
    }

  g_strstrip (contents);
  g_free (path);

  if (contents != NULL && *contents == '\0')
    g_clear_pointer (&contents, g_free);

  return contents;
}

static guint
dex_thread_pool_scheduler_get_n_cores (void)
{
  GHashTable *cores = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  gsize mask_size = sizeof (cpu_set_t);
  guint8 *mask = NULL;
  guint n_cores = 0;

  /* The kernel may have CPU IDs beyond CPU_SETSIZE. Grow the mask until it
   * can represent every CPU in this thread's affinity set.
   */
  for (;;)
    {
      g_free (mask);
      mask = g_malloc0 (mask_size);

      if (sched_getaffinity (0, mask_size, (cpu_set_t *)mask) == 0)
        break;

      if (errno != EINVAL || mask_size >= 1024 * 1024)
        goto failed;

      mask_size *= 2;
    }

  for (gsize byte = 0; byte < mask_size; byte++)
    {
      for (guint bit = 0; bit < 8; bit++)
        {
          guint cpu;
          char *siblings;

          if (!(mask[byte] & (1u << bit)))
            continue;

          cpu = byte * 8 + bit;

          /* Sibling CPUs report the same list, even if only one of them is
           * present in this thread's affinity mask.
           */
          if (!(siblings = dex_thread_pool_scheduler_get_core_siblings (cpu)))
            goto failed;

          if (g_hash_table_contains (cores, siblings))
            {
              g_free (siblings);
              continue;
            }

          g_hash_table_add (cores, siblings);

          if (++n_cores > MAX_WORKERS)
            goto done;
        }
    }

done:
  g_free (mask);
  g_hash_table_unref (cores);
  return n_cores;

failed:
  n_cores = 0;
  goto done;
}
#endif

static void
dex_thread_pool_scheduler_push (DexScheduler *scheduler,
                                DexWorkItem   work_item)
{
  DexThreadPoolScheduler *thread_pool_scheduler = DEX_THREAD_POOL_SCHEDULER (scheduler);
  DexThreadPoolWorker *worker = DEX_THREAD_POOL_WORKER_CURRENT;

  if (worker != NULL)
    DEX_SCHEDULER_GET_CLASS (worker)->push (DEX_SCHEDULER (worker), work_item);
  else
    dex_work_queue_push (thread_pool_scheduler->global_work_queue, work_item);
}

static GMainContext *
dex_thread_pool_scheduler_get_main_context (DexScheduler *scheduler)
{
  DexThreadPoolWorker *worker = DEX_THREAD_POOL_WORKER_CURRENT;

  /* Give the worker's main context if we're on a pooled thread */
  if (worker != NULL)
    return dex_scheduler_get_main_context (DEX_SCHEDULER (worker));

  /* Otherwise give the application default (main thread) context */
  return dex_scheduler_get_main_context (dex_scheduler_get_default ());
}

static DexAioContext *
dex_thread_pool_scheduler_get_aio_context (DexScheduler *scheduler)
{
  DexThreadPoolWorker *worker = DEX_THREAD_POOL_WORKER_CURRENT;

  /* Give the worker's aio context if we're on a pooled thread */
  if (worker != NULL)
    return dex_scheduler_get_aio_context (DEX_SCHEDULER (worker));

  /* Otherwise give the application default (main thread) aio context */
  return dex_scheduler_get_aio_context (dex_scheduler_get_default ());
}

static inline DexThreadPoolWorker *
rrobin_next (DexScheduler *scheduler)
{
  DexThreadPoolScheduler *thread_pool_scheduler = (DexThreadPoolScheduler *)scheduler;
  guint worker_index = g_atomic_int_add (&thread_pool_scheduler->fiber_rrobin, 1) % thread_pool_scheduler->n_workers;

  /* TODO: This is just doing a dumb round robin for assigning a fiber to a
   * specific thread pool worker. We probably want something more interesting
   * than that so we can have weighted workers or even keep affinity to a small
   * number of them until latency reaches some threshold.
   */

  return thread_pool_scheduler->workers[worker_index];
}

static void
dex_thread_pool_scheduler_spawn (DexScheduler *scheduler,
                                 DexFiber     *fiber)
{
  DexThreadPoolWorker *worker = rrobin_next (scheduler);

  DEX_SCHEDULER_GET_CLASS (worker)->spawn (DEX_SCHEDULER (worker), fiber);
}

static void
dex_thread_pool_scheduler_spawn_coroutine (DexScheduler *scheduler,
                                           DexCoroutine *coroutine)
{
  DexThreadPoolWorker *worker = rrobin_next (scheduler);

  DEX_SCHEDULER_GET_CLASS (worker)->spawn_coroutine (DEX_SCHEDULER (worker), coroutine);
}

static void
dex_thread_pool_scheduler_finalize (DexObject *object)
{
  DexThreadPoolScheduler *thread_pool_scheduler = (DexThreadPoolScheduler *)object;

  if ((DexScheduler *)thread_pool_scheduler == default_thread_pool)
    {
      g_critical ("Attempt to finalize default thread pool. "
                  "This should not happen and is an error in the application.");
      return;
    }

  for (guint i = 0; i < thread_pool_scheduler->n_workers; i++)
    dex_clear (&thread_pool_scheduler->workers[i]);

  dex_clear (&thread_pool_scheduler->global_work_queue);
  g_clear_pointer (&thread_pool_scheduler->set, dex_thread_pool_worker_set_unref);

  DEX_OBJECT_CLASS (dex_thread_pool_scheduler_parent_class)->finalize (object);
}

static void
dex_thread_pool_scheduler_class_init (DexThreadPoolSchedulerClass *thread_pool_scheduler_class)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (thread_pool_scheduler_class);
  DexSchedulerClass *scheduler_class = DEX_SCHEDULER_CLASS (thread_pool_scheduler_class);

  object_class->finalize = dex_thread_pool_scheduler_finalize;

  scheduler_class->get_main_context = dex_thread_pool_scheduler_get_main_context;
  scheduler_class->get_aio_context = dex_thread_pool_scheduler_get_aio_context;
  scheduler_class->push = dex_thread_pool_scheduler_push;
  scheduler_class->spawn = dex_thread_pool_scheduler_spawn;
  scheduler_class->spawn_coroutine = dex_thread_pool_scheduler_spawn_coroutine;
}

static void
dex_thread_pool_scheduler_init (DexThreadPoolScheduler *thread_pool_scheduler)
{
  thread_pool_scheduler->global_work_queue = dex_work_queue_new ();
  thread_pool_scheduler->set = dex_thread_pool_worker_set_new ();
}

guint
_dex_thread_pool_scheduler_get_n_workers (DexScheduler *scheduler)
{
  DexThreadPoolScheduler *thread_pool_scheduler;

  g_return_val_if_fail (DEX_IS_THREAD_POOL_SCHEDULER (scheduler), 0);

  thread_pool_scheduler = DEX_THREAD_POOL_SCHEDULER (scheduler);

  return thread_pool_scheduler->n_workers;
}

/**
 * dex_thread_pool_scheduler_new:
 *
 * Creates a new [class@Dex.Scheduler] that executes work items on a thread pool.
 * Worker threads inherit the calling thread's CPU affinity mask.
 *
 * Returns: (transfer full): a [class@Dex.ThreadPoolScheduler]
 */
DexScheduler *
dex_thread_pool_scheduler_new (void)
{
  DexThreadPoolScheduler *thread_pool_scheduler;
  guint n_cores = 0;
  guint n_workers;

  thread_pool_scheduler = (DexThreadPoolScheduler *)dex_object_create_instance (DEX_TYPE_THREAD_POOL_SCHEDULER);

  /* TODO: let this be dynamic and tunable, as well as thread pinning */

#ifdef __linux__
  n_cores = dex_thread_pool_scheduler_get_n_cores ();
#endif

  if (n_cores == 0)
    n_cores = MAX (1, g_get_num_processors ());

  /* Leave room for the main thread's AIO context. Apply the cap after
   * counting cores so a large machine can use all MAX_WORKERS slots.
   * Stop creating workers if one fails, for example due to io_uring limits.
   */
  n_workers = MIN (MAX_WORKERS, MAX (1, n_cores - 1));

  for (guint i = 0; i < n_workers; i++)
    {
      DexThreadPoolWorker *thread_pool_worker;

      thread_pool_worker = dex_thread_pool_worker_new (thread_pool_scheduler->global_work_queue,
                                                       thread_pool_scheduler->set,
                                                       i == 0);

      if (thread_pool_worker == NULL)
        break;

      thread_pool_scheduler->workers[thread_pool_scheduler->n_workers++] = thread_pool_worker;
    }

  g_assert (thread_pool_scheduler->n_workers > 0);

  atomic_thread_fence (memory_order_seq_cst);

  return DEX_SCHEDULER (thread_pool_scheduler);
}

/**
 * dex_thread_pool_scheduler_get_default:
 *
 * Gets the default thread pool scheduler for the instance.
 *
 * This function is useful to allow programs and libraries to share
 * an off-main-thread scheduler without having to coordinate on where
 * the scheduler instance is created or owned.
 * The pool is created on the first call, so its workers inherit that
 * caller's CPU affinity mask when they start.
 *
 * Returns: (transfer none): a [class@Dex.Scheduler]
 */
DexScheduler *
dex_thread_pool_scheduler_get_default (void)
{
  if (g_once_init_enter (&default_thread_pool))
    {
      DexScheduler *instance = dex_thread_pool_scheduler_new ();
      g_once_init_leave (&default_thread_pool, instance);
    }

  return default_thread_pool;
}
