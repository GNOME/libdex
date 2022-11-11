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

static GPrivate dex_scheduler_thread_default;

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
 * dex_scheduler_get_thread_default:
 *
 * Gets the default scheduler for the thread.
 *
 * Returns: (transfer none) (nullable): a #DexScheduler or %NULL
 */
DexScheduler *
dex_scheduler_get_thread_default (void)
{
  return g_private_get (&dex_scheduler_thread_default);
}

void
dex_scheduler_set_thread_default (DexScheduler *scheduler)
{
  g_private_set (&dex_scheduler_thread_default, scheduler);
}
