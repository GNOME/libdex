/*
 * dex-limiter.h
 *
 * Copyright 2026 Christian Hergert
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

#if !defined (DEX_INSIDE) && !defined (DEX_COMPILATION)
# error "Only <libdex.h> can be included directly."
#endif

#include "dex-scheduler.h"
#include "dex-thread-pool.h"
#include "dex-version-macros.h"

G_BEGIN_DECLS

#define DEX_TYPE_LIMITER    (dex_limiter_get_type())
#define DEX_LIMITER(obj)    (G_TYPE_CHECK_INSTANCE_CAST(obj, DEX_TYPE_LIMITER, DexLimiter))
#define DEX_IS_LIMITER(obj) (G_TYPE_CHECK_INSTANCE_TYPE(obj, DEX_TYPE_LIMITER))

typedef struct _DexLimiter DexLimiter;

DEX_AVAILABLE_IN_1_2
GType       dex_limiter_get_type            (void) G_GNUC_CONST;
DEX_AVAILABLE_IN_1_2
DexLimiter *dex_limiter_new                 (guint           max_concurrency);
DEX_AVAILABLE_IN_1_2
guint       dex_limiter_get_max_concurrency (DexLimiter     *limiter);
DEX_AVAILABLE_IN_1_2
DexFuture  *dex_limiter_acquire             (DexLimiter     *limiter) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
void        dex_limiter_release             (DexLimiter     *limiter);
DEX_AVAILABLE_IN_1_2
DexFuture  *dex_limiter_run                 (DexLimiter     *limiter,
                                             DexScheduler   *scheduler,
                                             gsize           stack_size,
                                             DexFiberFunc    func,
                                             gpointer        func_data,
                                             GDestroyNotify  func_data_destroy) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
DexFuture  *dex_limiter_run_on_pool         (DexLimiter     *limiter,
                                             DexThreadPool  *pool,
                                             DexThreadFunc   thread_func,
                                             gpointer        user_data,
                                             GDestroyNotify  user_data_destroy) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_2
void        dex_limiter_close               (DexLimiter     *limiter);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (DexLimiter, dex_unref)

G_END_DECLS
