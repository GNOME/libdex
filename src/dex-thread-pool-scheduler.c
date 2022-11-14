/*
 * dex-thread-pool-scheduler.c
 *
 * Copyright 2022 Christian Hergert <chergert@gnome.org>
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

#include "dex-scheduler-private.h"
#include "dex-thread-pool-scheduler.h"
#include "dex-thread-pool-worker-private.h"

struct _DexThreadPoolScheduler
{
  DexScheduler parent_instance;
};

typedef struct _DexThreadPoolSchedulerClass
{
  DexSchedulerClass parent_class;
} DexThreadPoolSchedulerClass;

DEX_DEFINE_FINAL_TYPE (DexThreadPoolScheduler, dex_thread_pool_scheduler, DEX_TYPE_SCHEDULER)

static void
dex_thread_pool_scheduler_push (DexScheduler     *scheduler,
                                DexSchedulerFunc  func,
                                gpointer          func_data)
{
  DexThreadPoolScheduler *thread_pool_scheduler = DEX_THREAD_POOL_SCHEDULER (scheduler);


}

static void
dex_thread_pool_scheduler_finalize (DexObject *object)
{
  DEX_OBJECT_CLASS (dex_thread_pool_scheduler_parent_class)->finalize (object);
}

static void
dex_thread_pool_scheduler_class_init (DexThreadPoolSchedulerClass *thread_pool_scheduler_class)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (thread_pool_scheduler_class);
  DexSchedulerClass *scheduler_class = DEX_SCHEDULER_CLASS (thread_pool_scheduler_class);

  object_class->finalize = dex_thread_pool_scheduler_finalize;

  scheduler_class->push = dex_thread_pool_scheduler_push;
}

static void
dex_thread_pool_scheduler_init (DexThreadPoolScheduler *thread_pool_scheduler)
{
}

/**
 * dex_thread_pool_scheduler_new:
 *
 * Creates a new #DexScheduler that executes work items on a thread pool.
 *
 * Returns: (transfer full): a #DexThreadPoolScheduler
 */
DexThreadPoolScheduler *
dex_thread_pool_scheduler_new (void)
{
  DexThreadPoolScheduler *thread_pool_scheduler;

  thread_pool_scheduler = (DexThreadPoolScheduler *)g_type_create_instance (DEX_TYPE_THREAD_POOL_SCHEDULER);

  return thread_pool_scheduler;
}
