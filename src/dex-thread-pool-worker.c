/*
 * dex-thread-pool-worker.c
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

#include "dex-thread-pool-worker-private.h"

DEX_DEFINE_FINAL_TYPE (DexThreadPoolWorker, dex_thread_pool_worker, DEX_TYPE_OBJECT)

static void
dex_thread_pool_worker_class_init (DexThreadPoolWorkerClass *thread_pool_worker_class)
{
}

static void
dex_thread_pool_worker_init (DexThreadPoolWorker *thread_pool_worker)
{
}

DexThreadPoolWorker *
dex_thread_pool_worker_new (void)
{
  DexThreadPoolWorker *thread_pool_worker;

  thread_pool_worker = (DexThreadPoolWorker *)g_type_create_instance (DEX_TYPE_THREAD_POOL_WORKER);

  return thread_pool_worker;
}
