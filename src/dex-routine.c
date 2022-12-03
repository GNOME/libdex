/*
 * dex-routine.c
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

#include "dex-error.h"
#include "dex-future-private.h"
#include "dex-routine-private.h"

struct _DexRoutine
{
  DexFuture parent_instance;
  DexRoutineFunc func;
  gpointer func_data;
  GDestroyNotify func_data_destroy;
};

typedef struct _DexRoutineClass
{
  DexFutureClass parent_class;
} DexRoutineClass;

DEX_DEFINE_FINAL_TYPE (DexRoutine, dex_routine, DEX_TYPE_FUTURE)

static void
dex_routine_finalize (DexObject *object)
{
  DexRoutine *routine = DEX_ROUTINE (object);

  if (routine->func_data_destroy)
    g_clear_pointer (&routine->func_data, routine->func_data_destroy);

  routine->func = NULL;
  routine->func_data_destroy = NULL;
  routine->func_data_destroy = NULL;

  DEX_OBJECT_CLASS (dex_routine_parent_class)->finalize (object);
}

static void
dex_routine_class_init (DexRoutineClass *routine_class)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (routine_class);

  object_class->finalize = dex_routine_finalize;
}

static void
dex_routine_init (DexRoutine *routine)
{
}

DexRoutine *
dex_routine_new (DexRoutineFunc func,
                 gpointer       func_data,
                 GDestroyNotify func_data_destroy)
{
  DexRoutine *routine;

  g_return_val_if_fail (func != NULL, NULL);

  /* TODO: We need finalizer control for @func_data_destroy so that we
   * can ensure some things are finalized in certain threads only.
   */

  routine = (DexRoutine *)g_type_create_instance (DEX_TYPE_ROUTINE);
  routine->func = func;
  routine->func_data = func_data;
  routine->func_data_destroy = func_data_destroy;

  return routine;
}

void
dex_routine_spawn (DexRoutine *routine)
{
  DexFuture *future;

  g_return_if_fail (DEX_IS_ROUTINE (routine));

  future = routine->func (routine->func_data);

  if (future == NULL)
    dex_future_complete (DEX_FUTURE (routine),
                         NULL,
                         g_error_new_literal (DEX_ERROR,
                                              DEX_ERROR_ROUTINE_COMPLETED,
                                              "Routine completed without a result"));
  else
    dex_future_chain (future, DEX_FUTURE (routine));

  dex_clear (&future);
}
