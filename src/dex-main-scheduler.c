/*
 * dex-main-scheduler.c
 *
 * Copyright 2022 Christian Hergert <chergert@redhat.com>
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

#include "config.h"

#include "dex-aio-backend-private.h"
#include "dex-fiber-private.h"
#include "dex-main-scheduler-private.h"
#include "dex-scheduler-private.h"
#include "dex-work-queue-private.h"
#include "dex-thread-storage-private.h"

typedef struct _DexMainScheduler
{
  DexScheduler  parent_scheduler;
  GMainContext *main_context;
  GSource      *aio_context;
  GSource      *fiber_scheduler;
  DexWorkQueue *work_queue;
  DexFuture    *work_queue_loop;
} DexMainScheduler;

typedef struct _DexMainSchedulerClass
{
  DexSchedulerClass parent_class;
} DexMainSchedulerClass;

DEX_DEFINE_FINAL_TYPE (DexMainScheduler, dex_main_scheduler, DEX_TYPE_SCHEDULER)

static void
dex_main_scheduler_push (DexScheduler *scheduler,
                         DexWorkItem   work_item)
{
  DexMainScheduler *main_scheduler = DEX_MAIN_SCHEDULER (scheduler);

  g_assert (DEX_IS_MAIN_SCHEDULER (main_scheduler));

  dex_work_queue_push (main_scheduler->work_queue, work_item);
}

static GMainContext *
dex_main_scheduler_get_main_context (DexScheduler *scheduler)
{
  DexMainScheduler *main_scheduler = DEX_MAIN_SCHEDULER (scheduler);

  g_assert (DEX_IS_MAIN_SCHEDULER (main_scheduler));

  return main_scheduler->main_context;
}

static DexAioContext *
dex_main_scheduler_get_aio_context (DexScheduler *scheduler)
{
  DexMainScheduler *main_scheduler = DEX_MAIN_SCHEDULER (scheduler);

  g_assert (DEX_IS_MAIN_SCHEDULER (main_scheduler));

  return (DexAioContext *)main_scheduler->aio_context;
}

static void
dex_main_scheduler_spawn_fiber (DexScheduler *scheduler,
                                DexFiber     *fiber)
{
  DexMainScheduler *main_scheduler = DEX_MAIN_SCHEDULER (scheduler);

  g_assert (DEX_IS_MAIN_SCHEDULER (main_scheduler));

  dex_fiber_migrate_to (fiber, (DexFiberScheduler *)main_scheduler->fiber_scheduler);
}

static void
dex_main_scheduler_finalize (DexObject *object)
{
  DexMainScheduler *main_scheduler = DEX_MAIN_SCHEDULER (object);

  dex_clear (&main_scheduler->work_queue);

  /* Clear DexAioBackend context */
  g_source_destroy (main_scheduler->aio_context);
  g_clear_pointer (&main_scheduler->aio_context, g_source_unref);

  /* Clear DexFiberScheduler context */
  g_source_destroy (main_scheduler->fiber_scheduler);
  g_clear_pointer (&main_scheduler->fiber_scheduler, g_source_unref);

  /* Release our main context */
  g_clear_pointer (&main_scheduler->main_context, g_main_context_unref);

  DEX_OBJECT_CLASS (dex_main_scheduler_parent_class)->finalize (object);
}

static void
dex_main_scheduler_class_init (DexMainSchedulerClass *main_scheduler_class)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (main_scheduler_class);
  DexSchedulerClass *scheduler_class = DEX_SCHEDULER_CLASS (main_scheduler_class);

  object_class->finalize = dex_main_scheduler_finalize;

  scheduler_class->get_aio_context = dex_main_scheduler_get_aio_context;
  scheduler_class->get_main_context = dex_main_scheduler_get_main_context;
  scheduler_class->push = dex_main_scheduler_push;
  scheduler_class->spawn_fiber = dex_main_scheduler_spawn_fiber;
}

static void
dex_main_scheduler_init (DexMainScheduler *main_scheduler)
{
}

DexMainScheduler *
dex_main_scheduler_new (GMainContext *main_context)
{
  DexFiberScheduler *fiber_scheduler;
  DexMainScheduler *main_scheduler;
  DexAioBackend *aio_backend;
  DexAioContext *aio_context;

  if (main_context == NULL)
    main_context = g_main_context_default ();

  aio_backend = dex_aio_backend_get_default ();
  aio_context = dex_aio_backend_create_context (aio_backend);

  fiber_scheduler = dex_fiber_scheduler_new ();

  main_scheduler = (DexMainScheduler *)g_type_create_instance (DEX_TYPE_MAIN_SCHEDULER);
  main_scheduler->main_context = g_main_context_ref (main_context);
  main_scheduler->aio_context = (GSource *)aio_context;
  main_scheduler->fiber_scheduler = (GSource *)fiber_scheduler;
  main_scheduler->work_queue = dex_work_queue_new ();

  dex_thread_storage_get ()->aio_context = aio_context;
  dex_thread_storage_get ()->scheduler = DEX_SCHEDULER (main_scheduler);

  main_scheduler->work_queue_loop = dex_work_queue_run (main_scheduler->work_queue);

  g_source_attach (main_scheduler->aio_context, main_context);
  g_source_attach (main_scheduler->fiber_scheduler, main_context);

  return main_scheduler;
}
