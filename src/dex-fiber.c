/* dex-fiber.c
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

#include <glib.h>

#include <stdarg.h>
#include <setjmp.h>
#include <ucontext.h>
#include <unistd.h>

#include "dex-error.h"
#include "dex-fiber-private.h"
#include "dex-object-private.h"
#include "dex-platform.h"

typedef struct _DexFiberClass
{
  DexFutureClass parent_class;
} DexFiberClass;

DEX_DEFINE_FINAL_TYPE (DexFiber, dex_fiber, DEX_TYPE_FUTURE)

static gboolean
dex_fiber_propagate (DexFuture *future,
                     DexFuture *completed)
{
  DexFiber *fiber = DEX_FIBER (future);
  gboolean ret = FALSE;

  g_assert (DEX_IS_FIBER (fiber));
  g_assert (DEX_IS_FUTURE (completed));

  dex_object_lock (fiber);

  if (fiber->state == DEX_FIBER_STATE_WAITING)
    {
      /* TODO: We need to migrate this fiber to the ready queue */

      fiber->state = DEX_FIBER_STATE_READY;
      ret = TRUE;
    }
  else if (fiber->state == DEX_FIBER_STATE_EXITED)
    {
      /* This is the final value for the fiber, pass it along */
    }
  else if (fiber->state == DEX_FIBER_STATE_WAITING)
    {
      g_warn_if_reached ();
    }
  else
    {
      g_assert_not_reached ();
    }

  dex_object_unlock (fiber);

  return ret;
}

static void
dex_fiber_finalize (DexObject *object)
{
  DexFiber *fiber = DEX_FIBER (object);

  if (fiber->fiber_scheduler != NULL)
    dex_fiber_migrate_to (fiber, NULL);

  g_clear_pointer (&fiber->stack, dex_stack_free);

  DEX_OBJECT_CLASS (dex_fiber_parent_class)->finalize (object);
}

static void
dex_fiber_class_init (DexFiberClass *fiber_class)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (fiber_class);
  DexFutureClass *future_class = DEX_FUTURE_CLASS (fiber_class);

  object_class->finalize = dex_fiber_finalize;

  future_class->propagate = dex_fiber_propagate;
}

static void
dex_fiber_init (DexFiber *fiber)
{
  fiber->link.data = fiber;
}

static void
dex_fiber_start (DexFiber *fiber)
{
  DexFuture *future;

  future = fiber->func (fiber->func_data);
  fiber->state = DEX_FIBER_STATE_EXITED;

  if (future == NULL)
    future = dex_future_new_reject (DEX_ERROR,
                                    DEX_ERROR_FIBER_EXITED,
                                    "The fiber exited without a result");

  dex_future_chain (future, DEX_FUTURE (fiber));

  if (fiber->fiber_scheduler)
    {
      DexFiberScheduler *fiber_scheduler = fiber->fiber_scheduler;

      dex_fiber_migrate_to (fiber, NULL);
      swapcontext (&fiber->context, &fiber_scheduler->context);
    }
}

static void
dex_fiber_start_ (int arg1, ...)
{
  DexFiber *fiber;

#if GLIB_SIZEOF_VOID_P == 4
  fiber = GSIZE_TO_POINTER (arg1);
#else
  va_list args;
  gsize hi;
  gsize lo;

  hi = arg1;
  va_start (args, arg1);
  lo = va_arg (args, int);
  va_end (args);

  fiber = GSIZE_TO_POINTER ((hi << 32) | lo);
#endif

  g_assert (DEX_IS_FIBER (fiber));

  dex_ref (fiber);
  dex_fiber_start (fiber);
  dex_unref (fiber);
}

DexFiber *
dex_fiber_new (DexFiberFunc func,
               gpointer     func_data,
               gsize        stack_size)
{
  DexFiber *fiber;
#if GLIB_SIZEOF_VOID_P == 8
  int lo;
  int hi;
#endif

  g_return_val_if_fail (func != NULL, NULL);

  fiber = (DexFiber *)g_type_create_instance (DEX_TYPE_FIBER);
  fiber->func = func;
  fiber->func_data = func_data;

  fiber->stack = dex_stack_new (stack_size);

  getcontext (&fiber->context);

  fiber->context.uc_stack.ss_size = fiber->stack->size;
  fiber->context.uc_stack.ss_sp = fiber->stack->base;
  fiber->context.uc_link = 0;

#if GLIB_SIZEOF_VOID_P == 8
  lo = GPOINTER_TO_SIZE (fiber) & 0xFFFFFFFFF;
  hi = (GPOINTER_TO_SIZE (fiber) >> 32) & 0xFFFFFFFFF;
#endif

  makecontext (&fiber->context,
               G_CALLBACK (dex_fiber_start_),
#if GLIB_SIZEOF_VOID_P == 4
               1, (gsize)fiber,
#else
               2, hi, lo
#endif
              );

  return fiber;
}

static gboolean
dex_fiber_scheduler_prepare (GSource *source,
                             int     *timeout)
{
  DexFiberScheduler *scheduler = (DexFiberScheduler *)source;

  *timeout = -1;

  return scheduler->ready.length > 0;
}

static gboolean
dex_fiber_scheduler_check (GSource *source)
{
  DexFiberScheduler *scheduler = (DexFiberScheduler *)source;

  return scheduler->ready.length > 0;
}

static gboolean
dex_fiber_scheduler_dispatch (GSource     *source,
                              GSourceFunc  callback,
                              gpointer     user_data)
{
  DexFiberScheduler *fiber_scheduler = (DexFiberScheduler *)source;

  g_assert (fiber_scheduler != NULL);

  g_rec_mutex_lock (&fiber_scheduler->rec_mutex);

  while (fiber_scheduler->ready.length > 0)
    {
      DexFiber *fiber = g_queue_pop_head_link (&fiber_scheduler->ready)->data;

      fiber_scheduler->current = fiber;
      swapcontext (&fiber_scheduler->context, &fiber->context);
      fiber_scheduler->current = NULL;
    }

  g_rec_mutex_unlock (&fiber_scheduler->rec_mutex);

  return G_SOURCE_CONTINUE;
}

static void
dex_fiber_scheduler_finalize (GSource *source)
{
  DexFiberScheduler *fiber_scheduler = (DexFiberScheduler *)source;

  g_rec_mutex_clear (&fiber_scheduler->rec_mutex);
}

/**
 * dex_fiber_scheduler_new:
 *
 * Creates a #DexFiberScheduler.
 *
 * #DexFiberScheduler is a sub-scheduler to a #DexScheduler that can swap
 * into and schedule runnable #DexFiber.
 *
 * A #DexScheduler should have one of these #GSource attached to it's
 * #GMainContext so that fibers can be executed there. When a thread
 * exits, it's fibers may need to be migrated. Currently that is not
 * implemented as we do not yet destroy #DexThreadPoolWorker.
 */
DexFiberScheduler *
dex_fiber_scheduler_new (void)
{
  DexFiberScheduler *fiber_scheduler;
  static GSourceFuncs funcs = {
    .check = dex_fiber_scheduler_check,
    .prepare = dex_fiber_scheduler_prepare,
    .dispatch = dex_fiber_scheduler_dispatch,
    .finalize = dex_fiber_scheduler_finalize,
  };

  fiber_scheduler = (DexFiberScheduler *)g_source_new (&funcs, sizeof *fiber_scheduler);
  g_source_set_static_name ((GSource *)fiber_scheduler, "[dex-fiber-scheduler]");
  g_rec_mutex_init (&fiber_scheduler->rec_mutex);

  return fiber_scheduler;
}

void
dex_fiber_migrate_to (DexFiber          *fiber,
                      DexFiberScheduler *fiber_scheduler)
{
  g_return_if_fail (DEX_IS_FIBER (fiber));

  dex_ref (fiber);
  dex_object_lock (fiber);

  if (fiber->fiber_scheduler != NULL)
    {
      g_rec_mutex_lock (&fiber->fiber_scheduler->rec_mutex);
      if (fiber->state == DEX_FIBER_STATE_READY)
        g_queue_unlink (&fiber->fiber_scheduler->ready, &fiber->link);
      else if (fiber->state == DEX_FIBER_STATE_WAITING)
        g_queue_unlink (&fiber->fiber_scheduler->waiting, &fiber->link);
      g_rec_mutex_unlock (&fiber->fiber_scheduler->rec_mutex);

      fiber->fiber_scheduler = NULL;
      dex_unref (fiber);
    }

  if (fiber->state != DEX_FIBER_STATE_EXITED && fiber_scheduler != NULL)
    {
      g_rec_mutex_lock (&fiber_scheduler->rec_mutex);
      if (fiber->state == DEX_FIBER_STATE_READY)
        g_queue_push_tail_link (&fiber_scheduler->ready, &fiber->link);
      else if (fiber->state == DEX_FIBER_STATE_WAITING)
        g_queue_push_tail_link (&fiber_scheduler->waiting, &fiber->link);
      g_rec_mutex_unlock (&fiber_scheduler->rec_mutex);

      fiber->fiber_scheduler = fiber_scheduler;
      dex_ref (fiber);
    }

  dex_object_unlock (fiber);
  dex_unref (fiber);
}
