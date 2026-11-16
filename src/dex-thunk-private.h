/* dex-thunk-private.h
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

#include "dex-scheduler.h"

G_BEGIN_DECLS

typedef struct _DexThunk DexThunk;

DexFuture *dex_thunk_dup_completion   (DexThunk            *thunk) G_GNUC_WARN_UNUSED_RESULT;
DexFuture *dex_thunk_invoke           (DexThunk            *thunk) G_GNUC_WARN_UNUSED_RESULT;
DexFuture *dex_thunk_invoke_callback  (DexThunk            *thunk,
                                       DexFuture           *completed) G_GNUC_WARN_UNUSED_RESULT;
DexFuture *dex_thunk_invoke_coroutine (DexThunk            *thunk,
                                       DexCoroutineContext *context) G_GNUC_WARN_UNUSED_RESULT;
DexThunk  *dex_thunk_new              (GCallback            callback,
                                       gpointer             user_data,
                                       GDestroyNotify       notify,
                                       DexScheduler        *scheduler);
DexThunk  *dex_thunk_ref              (DexThunk            *thunk);
void       dex_thunk_unref            (gpointer             data);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (DexThunk, dex_thunk_unref)

G_END_DECLS
