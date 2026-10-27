/* dex-deferred.h
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
#include "dex-scheduler.h"
#include "dex-thread-pool.h"

G_BEGIN_DECLS

#define DEX_TYPE_DEFERRED       (dex_deferred_get_type())
#define DEX_DEFERRED(object)    (G_TYPE_CHECK_INSTANCE_CAST(object, DEX_TYPE_DEFERRED, DexDeferred))
#define DEX_IS_DEFERRED(object) (G_TYPE_CHECK_INSTANCE_TYPE(object, DEX_TYPE_DEFERRED))

typedef struct _DexDeferred DexDeferred;

/**
 * DexDeferredFunc:
 * @user_data: closure data for the callback
 *
 * Called on the scheduler selected at construction when the deferred future
 * is first observed.
 *
 * Returns: (transfer full): a future whose result completes the deferred
 *
 * Since: 1.3
 */
typedef DexFuture *(*DexDeferredFunc) (gpointer user_data);

DEX_AVAILABLE_IN_1_3
GType        dex_deferred_get_type           (void);
DEX_AVAILABLE_IN_1_3
DexDeferred *dex_deferred_new                (DexScheduler     *scheduler,
                                              DexDeferredFunc   func,
                                              gpointer          user_data,
                                              GDestroyNotify    destroy) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexDeferred *dex_deferred_new_fiber          (DexScheduler     *scheduler,
                                              gsize             stack_size,
                                              DexFiberFunc      func,
                                              gpointer          user_data,
                                              GDestroyNotify    destroy) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexDeferred *dex_deferred_new_coroutine      (DexScheduler     *scheduler,
                                              DexCoroutineFunc  func,
                                              gpointer          user_data,
                                              GDestroyNotify    destroy) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexDeferred *dex_deferred_new_thread         (const char       *thread_name,
                                              DexThreadFunc     func,
                                              gpointer          user_data,
                                              GDestroyNotify    destroy) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexDeferred *dex_deferred_new_on_thread_pool (DexThreadPool    *pool,
                                              const char       *name,
                                              DexThreadFunc     func,
                                              gpointer          user_data,
                                              GDestroyNotify    destroy) G_GNUC_WARN_UNUSED_RESULT;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (DexDeferred, dex_unref)

G_END_DECLS
