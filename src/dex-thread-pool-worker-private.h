/*
 * dex-thread-pool-worker-private.h
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

#pragma once

#include "dex-object-private.h"
#include "dex-scheduler-private.h"
#include "dex-thread-pool-scheduler.h"

G_BEGIN_DECLS

#define DEX_TYPE_THREAD_POOL_WORKER       (dex_thread_pool_worker_get_type())
#define DEX_THREAD_POOL_WORKER(obj)       (G_TYPE_CHECK_INSTANCE_CAST(obj, DEX_TYPE_THREAD_POOL_WORKER, DexThreadPoolWorker))
#define DEX_IS_THREAD_POOL_WORKER(obj)    (G_TYPE_CHECK_INSTANCE_TYPE(obj, DEX_TYPE_THREAD_POOL_WORKER))
#define DEX_THREAD_POOL_WORKER_PRIORITY   G_PRIORITY_DEFAULT
#define DEX_THREAD_POOL_WORKER_BATCH_SIZE 10

typedef struct _DexThreadPoolWorker DexThreadPoolWorker;

GType                dex_thread_pool_worker_get_type (void) G_GNUC_CONST;
DexThreadPoolWorker *dex_thread_pool_worker_new      (DexThreadPoolScheduler *scheduler);
void                 dex_thread_pool_worker_push     (DexThreadPoolWorker    *worker,
                                                      DexWorkItem             work_item);
void                 dex_thread_pool_worker_attach   (DexThreadPoolWorker    *worker,
                                                      GSource                *source);

G_END_DECLS
