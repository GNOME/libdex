/*
 * dex-scheduler.c
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
#include "dex-thread-storage-private.h"

static DexScheduler *default_scheduler;

DEX_DEFINE_ABSTRACT_TYPE (DexScheduler, dex_scheduler, DEX_TYPE_OBJECT)

static void
dex_scheduler_class_init (DexSchedulerClass *scheduler_class)
{
}

static void
dex_scheduler_init (DexScheduler *scheduler)
{
}

/**
 * dex_scheduler_get_default:
 *
 * Gets the default scheduler for the process.
 *
 * The default scheduler executes tasks within the default #GMainContext.
 * Typically that is the main thread of the application.
 *
 * Returns: (transfer none) (not nullable): a #DexScheduler
 */
DexScheduler *
dex_scheduler_get_default (void)
{
  return default_scheduler;
}

void
dex_scheduler_set_default (DexScheduler *scheduler)
{
  g_return_if_fail (default_scheduler == NULL);
  g_return_if_fail (scheduler != NULL);

  default_scheduler = scheduler;
}

/**
 * dex_scheduler_get_thread_default:
 *
 * Gets the default scheduler for the thread.
 *
 * Returns: (transfer none) (nullable): a #DexScheduler or %NULL
 */
DexScheduler *
dex_scheduler_get_thread_default (void)
{
  return dex_thread_storage_get ()->scheduler;
}

void
dex_scheduler_set_thread_default (DexScheduler *scheduler)
{
  dex_thread_storage_get ()->scheduler = scheduler;
}

/**
 * dex_scheduler_ref_thread_default:
 *
 * Gets the thread default scheduler with the reference count incremented.
 *
 * Returns: (transfer full) (nullable): a #DexScheduler or %NULL
 */
DexScheduler *
dex_scheduler_ref_thread_default (void)
{
  DexScheduler *scheduler = dex_scheduler_get_thread_default ();

  if (scheduler != NULL)
    return dex_ref (scheduler);

  return NULL;
}

/**
 * dex_scheduler_push:
 * @scheduler: a #DexScheduler
 * @func: (call async): the function callback
 * @func_data: the closure data for @func
 *
 * Queues @func to run on @scheduler.
 */
void
dex_scheduler_push (DexScheduler     *scheduler,
                    DexSchedulerFunc  func,
                    gpointer          func_data)
{
  g_return_if_fail (DEX_IS_SCHEDULER (scheduler));
  g_return_if_fail (func != NULL);

  DEX_SCHEDULER_GET_CLASS (scheduler)->push (scheduler, (DexWorkItem) {func, func_data});
}

/**
 * dex_scheduler_attach:
 * @scheduler: a #DexScheduler
 * @source: a #GSource
 *
 * Attaches @source to the schedulers #GMainContext.
 *
 * This function will request that a #GSource is attached to a #GMainContext
 * that is processed by the scheduler. It is up to schedulers to determine
 * how this is processed such as the owning thread.
 *
 * Use g_source_destroy() to remove the source and g_source_unref() to
 * release your final reference to it.
 */
void
dex_scheduler_attach (DexScheduler *scheduler,
                      GSource      *source)
{
  DEX_SCHEDULER_GET_CLASS (scheduler)->attach (scheduler, source);
}
