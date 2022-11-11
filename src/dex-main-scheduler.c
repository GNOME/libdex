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
#include "dex-scheduler-private.h"

typedef struct _DexMainScheduler
{
  DexScheduler  parent_scheduler;
  GMainContext *main_context;
  GSource      *source;
  GQueue        queue;
} DexMainScheduler;

typedef struct _DexMainSchedulerClass
{
  DexSchedulerClass parent_class;
} DexMainSchedulerClass;

typedef struct _DexMainSource
{
  GSource  source;
  GQueue  *queue;
} DexMainSource;

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

static gboolean
dex_main_scheduler_dispatch (GSource     *source,
                             GSourceFunc  callback,
                             gpointer     user_data)
{
  return TRUE;
}

static GSourceFuncs main_source_funcs = {
  .dispatch = dex_main_scheduler_dispatch,
};

DexMainScheduler *
dex_main_scheduler_new (GMainContext *main_context)
{
  DexMainScheduler *main_scheduler;
  GSource *source;

  if (main_context == NULL)
    main_context = g_main_context_default ();

  source = g_source_new (&main_source_funcs, sizeof (DexMainSource));
  g_source_set_name (source, "[dex-main-scheduler]");
  g_source_set_priority (source, G_PRIORITY_HIGH);

  main_scheduler = (DexMainScheduler *)g_type_create_instance (DEX_TYPE_MAIN_SCHEDULER);
  main_scheduler->main_context = g_main_context_ref (main_context);
  main_scheduler->source = source;

  ((DexMainSource *)source)->queue = &main_scheduler->queue;

  g_source_attach (source, main_context);

  return main_scheduler;
}
