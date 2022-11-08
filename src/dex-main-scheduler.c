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

#include "dex-main-scheduler.h"
#include "dex-private.h"

typedef struct _DexMainScheduler
{
  DexScheduler  parent_scheduler;
  GMainContext *main_context;
} DexMainScheduler;

typedef struct _DexMainSchedulerClass
{
  DexSchedulerClass parent_class;
} DexMainSchedulerClass;

DEX_DEFINE_FINAL_TYPE (DexMainScheduler, dex_main_scheduler, DEX_TYPE_SCHEDULER)

static void
dex_main_scheduler_finalize (DexObject *object)
{
  DexMainScheduler *main_scheduler = DEX_MAIN_SCHEDULER (object);

  g_clear_pointer (&main_scheduler->main_context, g_main_context_unref);

  DEX_OBJECT_CLASS (dex_main_scheduler_parent_class)->finalize (object);
}

static void
dex_main_scheduler_class_init (DexMainSchedulerClass *main_scheduler_class)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (main_scheduler_class);

  object_class->finalize = dex_main_scheduler_finalize;
}

static void
dex_main_scheduler_init (DexMainScheduler *main_scheduler)
{
}

DexMainScheduler *
dex_main_scheduler_new (GMainContext *main_context)
{
  DexMainScheduler *main_scheduler;

  if (main_context == NULL)
    main_context = g_main_context_default ();

  main_scheduler = (DexMainScheduler *)g_type_create_instance (DEX_TYPE_MAIN_SCHEDULER);
  main_scheduler->main_context = g_main_context_ref (main_context);

  /* TODO: Add GSource for dispatching once we have API figured out for DexScheduler */

  return main_scheduler;
}
