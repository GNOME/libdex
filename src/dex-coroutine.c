/*
 * dex-coroutine.c
 *
 * Copyright 2026 Christian Hergert
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "dex-coroutine.h"
#include "dex-coroutine-private.h"
#include "dex-error.h"
#include "dex-future-private.h"
#include "dex-compat-private.h"
#include "dex-thread-storage-private.h"

/**
 * DexCoroutine:
 *
 * `DexCoroutine` is a [class@Dex.Future] implemented as a stateful stackless
 * coroutine cooperatively scheduled on a [class@Dex.Scheduler].
 *
 * Each coroutine stores stackful suspension state and optionally accepts user
 * data via `user_data`.
 *
 * Use user data passed at spawn time for stateful data.
 */

typedef enum _DexCoroutineQueue
{
  CORO_QUEUE_NONE,
  CORO_QUEUE_RUNNABLE,
  CORO_QUEUE_BLOCKED,
  CORO_QUEUE_LAST,
} DexCoroutineQueue;

typedef struct _DexCoroutineClass
{
  DexFutureClass parent_class;
} DexCoroutineClass;

typedef struct _DexCoroutineScheduler
{
  GSource       source;
  GMutex        mutex;
  DexCoroutine *running;
  GQueue        runnable;
  GQueue        blocked;
} DexCoroutineScheduler;

struct _DexCoroutineContext
{
  DexCoroutine      *coroutine;
  guint              pc;
  DexFuture         *pending;
};

struct _DexCoroutine
{
  DexFuture              parent_instance;
  GList                  link;
  DexCoroutineFunc       func;
  DexCoroutineContext    context;
  gpointer               user_data;
  DexCoroutineScheduler *coroutine_scheduler;
  DexCoroutineQueue      queue : 2;
  guint                  running : 1;
  guint                  runnable : 1;
  guint                  cancelled : 1;
  guint                  exited : 1;
  guint                  released : 1;
  guint                  awaiting_final : 1;
};

static void     dex_coroutine_resume                (DexCoroutine          *coroutine);
static void     dex_coroutine_discard               (DexFuture             *future);
static gboolean dex_coroutine_propagate             (DexFuture             *future,
                                                     DexFuture             *completed);
static void     dex_coroutine_complete_cancelled    (DexCoroutine          *coroutine);
static void     dex_coroutine_complete_error        (DexCoroutine          *coroutine,
                                                     GError                *error);
static void     dex_coroutine_complete_from_future  (DexCoroutine          *coroutine,
                                                     DexFuture             *future);
static void     dex_coroutine_wait_for              (DexCoroutine          *coroutine,
                                                     DexFuture             *future,
                                                     gboolean               awaiting_final);
static void     dex_coroutine_clear_pending         (DexCoroutine          *coroutine);
static void     dex_coroutine_set_queue             (DexCoroutineScheduler *scheduler,
                                                     DexCoroutine          *coroutine,
                                                     DexCoroutineQueue      queue);
static void     dex_coroutine_detach_from_scheduler (DexCoroutine          *coroutine);

DEX_DEFINE_FINAL_TYPE (DexCoroutine, dex_coroutine, DEX_TYPE_FUTURE)

#undef DEX_TYPE_COROUTINE
#define DEX_TYPE_COROUTINE dex_coroutine_type

static void
dex_coroutine_init (DexCoroutine *coroutine)
{
  coroutine->context.coroutine = coroutine;
  coroutine->link.data = coroutine;
}

/**
 * dex_coroutine_suspend: (skip)
 * @context: the coroutine context
 * @pc: the state machine PC
 * @future: (transfer full): the future to suspend for
 *
 * Since: 1.2
 */
void
dex_coroutine_context_suspend (DexCoroutineContext *context,
                               guint                pc,
                               DexFuture           *future)
{
  DexCoroutine *coroutine;

  g_return_if_fail (context != NULL);
  g_return_if_fail (future != NULL);

  coroutine = context->coroutine;

  g_return_if_fail (coroutine != NULL);
  g_return_if_fail (DEX_IS_COROUTINE (coroutine));

  g_clear_pointer (&context->pending, dex_unref);
  context->pc = pc;

  g_assert (future != DEX_FUTURE (coroutine));

  context->pending = g_steal_pointer (&future);
}

/**
 * dex_coroutine_resume: (skip)
 * @context: the coroutine context
 * @pc: (out): the state machine PC
 * @future: (out) (transfer full): the future which resumed
 *
 * Since: 1.2
 */
void
dex_coroutine_context_resume (DexCoroutineContext *context,
                              guint              *pc,
                              DexFuture         **future)
{
  DexCoroutine *coroutine;

  g_return_if_fail (context != NULL);
  g_return_if_fail (pc != NULL);
  g_return_if_fail (future != NULL);

  coroutine = context->coroutine;
  g_return_if_fail (coroutine != NULL);
  g_return_if_fail (DEX_IS_COROUTINE (coroutine));

  *pc = context->pc;
  *future = g_steal_pointer (&context->pending);
}

static void
dex_coroutine_finalize (DexObject *object)
{
  DexCoroutine *coroutine = DEX_COROUTINE (object);

  g_assert (coroutine->link.prev == NULL);
  g_assert (coroutine->link.next == NULL);
  g_assert (coroutine->queue == CORO_QUEUE_NONE);
  g_assert (coroutine->coroutine_scheduler == NULL);
  g_assert (coroutine->link.data == coroutine);

  dex_coroutine_clear_pending (coroutine);

  DEX_OBJECT_CLASS (dex_coroutine_parent_class)->finalize (object);
}

static void
dex_coroutine_class_init (DexCoroutineClass *coroutine_class)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (coroutine_class);
  DexFutureClass *future_class = DEX_FUTURE_CLASS (coroutine_class);

  object_class->finalize = dex_coroutine_finalize;
  future_class->discard = dex_coroutine_discard;
  future_class->propagate = dex_coroutine_propagate;
}

static void
dex_coroutine_set_queue (DexCoroutineScheduler *scheduler,
                         DexCoroutine          *coroutine,
                         DexCoroutineQueue      queue)
{
  g_assert (scheduler != NULL);
  g_assert (coroutine != NULL);
  g_assert (queue < CORO_QUEUE_LAST);

  if (coroutine->queue == queue)
    return;

  if (coroutine->queue == CORO_QUEUE_RUNNABLE)
    g_queue_unlink (&scheduler->runnable, &coroutine->link);
  else if (coroutine->queue == CORO_QUEUE_BLOCKED)
    g_queue_unlink (&scheduler->blocked, &coroutine->link);

  coroutine->queue = queue;

  if (queue == CORO_QUEUE_RUNNABLE)
    {
      coroutine->runnable = TRUE;
      g_queue_push_tail_link (&scheduler->runnable, &coroutine->link);
    }
  else if (queue == CORO_QUEUE_BLOCKED)
    {
      coroutine->runnable = FALSE;
      g_queue_push_tail_link (&scheduler->blocked, &coroutine->link);
    }
  else
    {
      coroutine->runnable = FALSE;
      coroutine->link.prev = NULL;
      coroutine->link.next = NULL;
      coroutine->link.data = coroutine;
    }
}

static void
dex_coroutine_detach_from_scheduler (DexCoroutine *coroutine)
{
  DexCoroutineScheduler *scheduler = coroutine->coroutine_scheduler;

  if (scheduler == NULL)
    return;

  g_mutex_lock (&scheduler->mutex);
  if (coroutine->queue == CORO_QUEUE_RUNNABLE ||
      coroutine->queue == CORO_QUEUE_BLOCKED)
    dex_coroutine_set_queue (scheduler, coroutine, CORO_QUEUE_NONE);
  coroutine->coroutine_scheduler = NULL;
  g_mutex_unlock (&scheduler->mutex);
}

static void
dex_coroutine_clear_pending (DexCoroutine *coroutine)
{
  g_clear_pointer (&coroutine->context.pending, dex_unref);
}

static void
dex_coroutine_complete_error (DexCoroutine *coroutine,
                              GError       *error)
{
  g_return_if_fail (coroutine != NULL);

  coroutine->awaiting_final = FALSE;
  coroutine->exited = TRUE;

  dex_coroutine_clear_pending (coroutine);
  dex_coroutine_detach_from_scheduler (coroutine);

  if (error == NULL)
    error = g_error_new_literal (DEX_ERROR,
                                 DEX_ERROR_FIBER_EXITED,
                                 "Coroutine exited without a result");

  dex_future_complete (DEX_FUTURE (coroutine), NULL, error);
}

static void
dex_coroutine_complete_from_future (DexCoroutine *coroutine,
                                    DexFuture    *future)
{
  g_assert (DEX_IS_COROUTINE (coroutine));
  g_assert (DEX_IS_FUTURE (future));

  coroutine->awaiting_final = FALSE;
  coroutine->exited = TRUE;

  dex_coroutine_clear_pending (coroutine);
  dex_coroutine_detach_from_scheduler (coroutine);

  dex_future_complete_from (DEX_FUTURE (coroutine), future);
}

static void
dex_coroutine_complete_cancelled (DexCoroutine *coroutine)
{
  g_assert (coroutine != NULL);

  dex_coroutine_complete_error (coroutine,
                                g_error_new_literal (DEX_ERROR,
                                                     DEX_ERROR_FIBER_CANCELLED,
                                                     "Coroutine was cancelled"));
}

static void
dex_coroutine_wait_for (DexCoroutine *coroutine,
                        DexFuture    *future,
                        gboolean      awaiting_final)
{
  g_assert (coroutine != NULL);
  g_assert (DEX_IS_COROUTINE (coroutine));
  g_assert (future != NULL);
  g_assert (!coroutine->exited);
  g_assert (!coroutine->cancelled);

  dex_clear (&coroutine->context.pending);
  coroutine->context.pending = g_steal_pointer (&future);

  coroutine->awaiting_final = awaiting_final;
  dex_future_chain (coroutine->context.pending, DEX_FUTURE (coroutine));

  if (coroutine->coroutine_scheduler != NULL)
    {
      GSource *source;
      DexThreadStorage *thread_storage;

      g_mutex_lock (&coroutine->coroutine_scheduler->mutex);
      dex_coroutine_set_queue (coroutine->coroutine_scheduler,
                               coroutine,
                               CORO_QUEUE_BLOCKED);
      thread_storage = dex_thread_storage_get ();
      source = thread_storage->coroutine_scheduler == coroutine->coroutine_scheduler
                 ? NULL
                 : g_source_ref ((GSource *)coroutine->coroutine_scheduler);
      g_mutex_unlock (&coroutine->coroutine_scheduler->mutex);

      if (source != NULL)
        {
          g_main_context_wakeup (g_source_get_context (source));
          g_source_unref (source);
        }
    }
}

static void
dex_coroutine_resume (DexCoroutine *coroutine)
{
  DexFuture *future;

  g_assert (coroutine != NULL);

  if (coroutine->cancelled)
    {
      dex_coroutine_complete_cancelled (coroutine);
      return;
    }

  g_assert (!coroutine->context.pending || coroutine->context.pc > 0);

  if ((future = coroutine->func (&coroutine->context, coroutine->user_data)))
    {
      if (dex_future_is_pending (future))
        {
          if (coroutine->cancelled)
            {
              dex_unref (future);
              dex_coroutine_complete_cancelled (coroutine);
            }
          else
            {
              dex_coroutine_wait_for (coroutine, g_steal_pointer (&future), TRUE);
            }
        }
        else
        {
          DexFuture *completed = g_steal_pointer (&future);
          dex_coroutine_complete_from_future (coroutine, completed);
          dex_unref (completed);
        }

      return;
    }

  if (coroutine->context.pending == NULL)
    dex_coroutine_complete_error (coroutine, NULL);
  else if (dex_future_is_pending (coroutine->context.pending))
    dex_coroutine_wait_for (coroutine, g_steal_pointer (&coroutine->context.pending), FALSE);
  else
    dex_coroutine_complete_from_future (coroutine, g_steal_pointer (&coroutine->context.pending));
}

static gboolean
dex_coroutine_scheduler_check (GSource *source)
{
  DexCoroutineScheduler *scheduler = (DexCoroutineScheduler *)source;
  gboolean ret;

  g_mutex_lock (&scheduler->mutex);
  g_assert (scheduler->runnable.length == 0 || scheduler->runnable.head != NULL);
  g_assert (scheduler->runnable.length > 0 || scheduler->runnable.head == NULL);
  ret = scheduler->runnable.head != NULL;
  g_mutex_unlock (&scheduler->mutex);

  return ret;
}

static gboolean
dex_coroutine_scheduler_prepare (GSource *source,
                                 int     *timeout)
{
  *timeout = -1;

  return dex_coroutine_scheduler_check (source);
}

static gboolean
dex_coroutine_scheduler_iteration (DexCoroutineScheduler *scheduler)
{
  DexCoroutine *coroutine;
  gboolean ret;
  gboolean release = FALSE;

  g_mutex_lock (&scheduler->mutex);
  if ((coroutine = g_queue_peek_head (&scheduler->runnable)))
    {
      g_queue_unlink (&scheduler->runnable, &coroutine->link);

      g_assert (coroutine->queue == CORO_QUEUE_RUNNABLE);
      g_assert (coroutine->link.data == coroutine);

      coroutine->queue = CORO_QUEUE_NONE;
      coroutine->running = TRUE;
      coroutine->coroutine_scheduler = scheduler;
      scheduler->running = coroutine;
    }
  g_mutex_unlock (&scheduler->mutex);

  if (coroutine == NULL)
    return FALSE;

  dex_coroutine_resume (coroutine);

  g_mutex_lock (&scheduler->mutex);
  coroutine->running = FALSE;
  scheduler->running = NULL;

  if (coroutine->exited)
    {
      coroutine->coroutine_scheduler = NULL;
      coroutine->released = TRUE;
      release = TRUE;
    }

  ret = scheduler->runnable.length > 0;
  g_mutex_unlock (&scheduler->mutex);

  if (release)
    dex_unref (coroutine);

  return ret;
}

static gboolean
dex_coroutine_scheduler_dispatch (GSource     *source,
                                  GSourceFunc  callback,
                                  gpointer     callback_data)
{
  DexCoroutineScheduler *scheduler = (DexCoroutineScheduler *)source;
  guint max_iterations;
  DexThreadStorage *thread_storage;

  g_assert (scheduler != NULL);

  g_mutex_lock (&scheduler->mutex);
  max_iterations = MAX (1, scheduler->runnable.length);
  g_mutex_unlock (&scheduler->mutex);

  thread_storage = dex_thread_storage_get ();
  thread_storage->coroutine_scheduler = scheduler;
  while (max_iterations && dex_coroutine_scheduler_iteration (scheduler))
    max_iterations--;
  thread_storage->coroutine_scheduler = NULL;

  return G_SOURCE_CONTINUE;
}

static void
dex_coroutine_scheduler_finalize (GSource *source)
{
  DexCoroutineScheduler *scheduler = (DexCoroutineScheduler *)source;

  while (scheduler->runnable.length > 0)
    {
      DexCoroutine *routine = g_queue_peek_head (&scheduler->runnable);
      g_queue_unlink (&scheduler->runnable, &routine->link);
      dex_unref (routine);
    }

  while (scheduler->blocked.length > 0)
    {
      DexCoroutine *routine = g_queue_peek_head (&scheduler->blocked);
      g_queue_unlink (&scheduler->blocked, &routine->link);
      dex_unref (routine);
    }

  g_mutex_clear (&scheduler->mutex);
}

static GSourceFuncs source_funcs = {
  .check = dex_coroutine_scheduler_check,
  .prepare = dex_coroutine_scheduler_prepare,
  .dispatch = dex_coroutine_scheduler_dispatch,
  .finalize = dex_coroutine_scheduler_finalize,
};

/**
 * dex_coroutine_scheduler_new:
 *
 * Creates a coroutine scheduler source.
 *
 * Returns: (transfer full):
 */
DexCoroutineScheduler *
dex_coroutine_scheduler_new (void)
{
  DexCoroutineScheduler *scheduler;

  scheduler = (DexCoroutineScheduler *)g_source_new (&source_funcs, sizeof *scheduler);
  _g_source_set_static_name ((GSource *)scheduler, "[dex-coroutine-scheduler]");
  g_mutex_init (&scheduler->mutex);

  return scheduler;
}

void
dex_coroutine_scheduler_register (DexCoroutineScheduler *scheduler,
                                 DexCoroutine          *coroutine)
{
  g_return_if_fail (scheduler != NULL);
  g_return_if_fail (DEX_IS_COROUTINE (coroutine));

  dex_ref (coroutine);

  g_mutex_lock (&scheduler->mutex);

  g_assert (!coroutine->running);
  g_assert (!coroutine->exited);
  g_assert (coroutine->queue == CORO_QUEUE_NONE);
  g_assert (coroutine->coroutine_scheduler == NULL);

  coroutine->coroutine_scheduler = scheduler;
  dex_coroutine_set_queue (scheduler, coroutine, CORO_QUEUE_RUNNABLE);

  g_mutex_unlock (&scheduler->mutex);

  if (dex_thread_storage_get ()->coroutine_scheduler != scheduler)
    g_main_context_wakeup (g_source_get_context ((GSource *)scheduler));
}

static void
dex_coroutine_discard (DexFuture *future)
{
  DexCoroutine *coroutine = DEX_COROUTINE (future);
  DexFuture *pending = NULL;

  g_return_if_fail (coroutine != NULL);

  dex_object_lock (coroutine);

  if (coroutine->exited)
    {
      dex_object_unlock (coroutine);
      return;
    }

  coroutine->cancelled = TRUE;
  pending = g_steal_pointer (&coroutine->context.pending);

  if (coroutine->running)
    {
      dex_object_unlock (coroutine);
      return;
    }

  dex_coroutine_detach_from_scheduler (coroutine);

  coroutine->awaiting_final = FALSE;
  coroutine->exited = TRUE;
  dex_object_unlock (coroutine);

  if (pending != NULL)
    {
      if (dex_future_is_pending (pending))
        dex_future_discard (pending, DEX_FUTURE (coroutine));
      dex_unref (pending);
    }

  dex_coroutine_complete_cancelled (coroutine);
}

static gboolean
dex_coroutine_propagate (DexFuture *future,
                         DexFuture *completed)
{
  DexCoroutine *coroutine = DEX_COROUTINE (future);

  g_assert (coroutine != NULL);
  g_assert (completed != NULL);

  if (coroutine->cancelled)
    {
      dex_coroutine_complete_cancelled (coroutine);
      return TRUE;
    }

  if (!coroutine->awaiting_final)
    {
      if (coroutine->coroutine_scheduler != NULL)
        {
          GSource *source;
          DexThreadStorage *thread_storage;

          g_mutex_lock (&coroutine->coroutine_scheduler->mutex);
          dex_coroutine_set_queue (coroutine->coroutine_scheduler,
                                   coroutine,
                                   CORO_QUEUE_RUNNABLE);
          thread_storage = dex_thread_storage_get ();
          source = thread_storage->coroutine_scheduler == coroutine->coroutine_scheduler
                     ? NULL
                     : g_source_ref ((GSource *)coroutine->coroutine_scheduler);
          g_mutex_unlock (&coroutine->coroutine_scheduler->mutex);

          if (source != NULL)
            {
              g_main_context_wakeup (g_source_get_context (source));
              g_source_unref (source);
            }
        }

      return TRUE;
    }

  coroutine->context.pending = NULL;

  coroutine->awaiting_final = FALSE;
  coroutine->exited = TRUE;

  dex_coroutine_detach_from_scheduler (coroutine);

  if (coroutine->cancelled)
    dex_coroutine_complete_cancelled (coroutine);
  else
    dex_coroutine_complete_from_future (coroutine, completed);

  return TRUE;
}

DexCoroutine *
dex_coroutine_new (DexCoroutineFunc func,
                   gpointer         user_data)
{
  DexCoroutine *coroutine;

  g_return_val_if_fail (func != NULL, NULL);

  coroutine = (DexCoroutine *)dex_object_create_instance (DEX_TYPE_COROUTINE);
  coroutine->func = func;
  coroutine->user_data = user_data;

  return coroutine;
}
