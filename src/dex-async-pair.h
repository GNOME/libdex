/*
 * dex-async-pair.h
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

#include "dex-future.h"

G_BEGIN_DECLS

#define DEX_TYPE_ASYNC_PAIR    (dex_async_pair_get_type())
#define DEX_ASYNC_PAIR(obj)    (G_TYPE_CHECK_INSTANCE_CAST(obj, DEX_TYPE_ASYNC_PAIR, DexAsyncPair))
#define DEX_IS_ASYNC_PAIR(obj) (G_TYPE_CHECK_INSTANCE_TYPE(obj, DEX_TYPE_ASYNC_PAIR))

typedef struct _DexAsyncPair DexAsyncPair;

typedef struct _DexAsyncPairInfo
{
  gpointer  async;
  gpointer  finish;
  GType     return_type;
} DexAsyncPairInfo;

#define DEX_ASYNC_PAIR_INFO(Async, Finish, ReturnType) \
  (DexAsyncPairInfo) {                                 \
    .async = Async,                                    \
    .finish = Finish,                                  \
    .return_type = ReturnType,                         \
  }

#define DEX_ASYNC_PAIR_INFO_BOOLEAN(Async, Finish)     \
  DEX_ASYNC_PAIR_INFO (Async, Finish, G_TYPE_BOOLEAN)

#define DEX_ASYNC_PAIR_INFO_INT(Async, Finish)         \
  DEX_ASYNC_PAIR_INFO (Async, Finish, G_TYPE_INT)

#define DEX_ASYNC_PAIR_INFO_UINT(Async, Finish)        \
  DEX_ASYNC_PAIR_INFO (Async, Finish, G_TYPE_UINT)

#define DEX_ASYNC_PAIR_INFO_STRING(Async, Finish)      \
  DEX_ASYNC_PAIR_INFO (Async, Finish, G_TYPE_STRING)

#define DEX_ASYNC_PAIR_INFO_OBJECT(Async, Finish)      \
  DEX_ASYNC_PAIR_INFO (Async, Finish, G_TYPE_OBJECT)

DEX_AVAILABLE_IN_ALL
GType      dex_async_pair_get_type (void) G_GNUC_CONST;
DEX_AVAILABLE_IN_ALL
DexFuture *dex_async_pair_new      (gpointer                instance,
                                    const DexAsyncPairInfo *info);

G_END_DECLS
