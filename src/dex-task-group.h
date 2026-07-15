/*
 * dex-task-group.h
 *
 * Copyright 2026 Christian Hergert
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#if !defined (DEX_INSIDE) && !defined (DEX_COMPILATION)
# error "Only <libdex.h> can be included directly."
#endif

#include "dex-future.h"

G_BEGIN_DECLS

#define DEX_TYPE_TASK_GROUP       (dex_task_group_get_type())
#define DEX_TASK_GROUP(object)    (G_TYPE_CHECK_INSTANCE_CAST(object, DEX_TYPE_TASK_GROUP, DexTaskGroup))
#define DEX_IS_TASK_GROUP(object) (G_TYPE_CHECK_INSTANCE_TYPE(object, DEX_TYPE_TASK_GROUP))

typedef struct _DexTaskGroup DexTaskGroup;

typedef enum _DexTaskGroupFlags
{
  DEX_TASK_GROUP_FLAGS_NONE            = 0,
  DEX_TASK_GROUP_FLAGS_CANCEL_ON_ERROR = 1 << 0,
} DexTaskGroupFlags;

DEX_AVAILABLE_IN_ALL
GType         dex_task_group_get_type            (void);
DEX_AVAILABLE_IN_ALL
DexTaskGroup *dex_task_group_new                 (DexTaskGroupFlags  flags);
DEX_AVAILABLE_IN_ALL
gboolean      dex_task_group_add                 (DexTaskGroup      *group,
                                                  DexFuture         *future);
DEX_AVAILABLE_IN_ALL
DexFuture    *dex_task_group_close               (DexTaskGroup      *group) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
void          dex_task_group_cancel              (DexTaskGroup      *group);
DEX_AVAILABLE_IN_ALL
void          dex_task_group_push_thread_default (DexTaskGroup      *group);
DEX_AVAILABLE_IN_ALL
void          dex_task_group_pop_thread_default  (DexTaskGroup      *group);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (DexTaskGroup, dex_unref)

G_END_DECLS
