/* dex-thread-pool.h
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#if !defined (DEX_INSIDE) && !defined (DEX_COMPILATION)
# error "Only <libdex.h> can be included directly."
#endif

#include "dex-future.h"
#include "dex-thread.h"
#include "dex-version-macros.h"

G_BEGIN_DECLS

#define DEX_TYPE_THREAD_POOL    (dex_thread_pool_get_type())
#define DEX_THREAD_POOL(obj)    (G_TYPE_CHECK_INSTANCE_CAST(obj, DEX_TYPE_THREAD_POOL, DexThreadPool))
#define DEX_IS_THREAD_POOL(obj) (G_TYPE_CHECK_INSTANCE_TYPE(obj, DEX_TYPE_THREAD_POOL))

typedef struct _DexThreadPool DexThreadPool;

typedef enum
{
  DEX_THREAD_POOL_SHUTDOWN_DRAIN,
  DEX_THREAD_POOL_SHUTDOWN_CANCEL_QUEUED,
} DexThreadPoolShutdownMode;

DEX_AVAILABLE_IN_1_2
GType          dex_thread_pool_get_type      (void) G_GNUC_CONST;
DEX_AVAILABLE_IN_1_2
DexThreadPool *dex_thread_pool_new           (guint                      n_threads);
DEX_AVAILABLE_IN_1_2
guint          dex_thread_pool_get_n_threads (DexThreadPool             *pool);
DEX_AVAILABLE_IN_1_2
DexFuture     *dex_thread_pool_submit        (DexThreadPool             *pool,
                                              const char                *thread_name,
                                              DexThreadFunc              thread_func,
                                              gpointer                   user_data,
                                              GDestroyNotify             user_data_destroy) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture     *dex_thread_pool_close         (DexThreadPool             *pool,
                                              DexThreadPoolShutdownMode  mode) G_GNUC_WARN_UNUSED_RESULT;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (DexThreadPool, dex_unref)

G_END_DECLS
