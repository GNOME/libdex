/*
 * dex-private.h
 *
 * Copyright 2022 Christian Hergert <chergert@gnome.org>
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
#include "dex-future.h"
#include "dex-future-set.h"

G_BEGIN_DECLS

typedef struct _DexCallable
{
  DexObject parent_instance;
} DexCallable;

typedef struct _DexCallableClass
{
  DexObjectClass parent_class;
} DexCallableClass;

typedef struct _DexFunction
{
  DexCallable parent_class;
} DexFunction;

typedef struct _DexFunctionClass
{
  DexCallableClass parent_class;
} DexFunctionClass;

#define DEX_FUTURE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST(klass, DEX_TYPE_FUTURE, DexFutureClass))
#define DEX_FUTURE_GET_CLASS(obj) \
  G_TYPE_INSTANCE_GET_CLASS(obj, DEX_TYPE_FUTURE, DexFutureClass)

typedef struct _DexFuture
{
  DexObject parent_instance;
  GValue resolved;
  GError *rejected;
  GList *chained;
  DexFutureStatus status : 2;
} DexFuture;

typedef struct _DexFutureClass
{
  DexObjectClass parent_class;

  gboolean (*propagate) (DexFuture *future,
                         DexFuture *completed);
} DexFutureClass;

void dex_future_chain         (DexFuture    *future,
                               DexFuture    *chained);
void dex_future_complete      (DexFuture    *future,
                               const GValue *value,
                               GError       *error);
void dex_future_complete_from (DexFuture    *future,
                               DexFuture    *completed);

typedef enum _DexFutureSetFlags
{
  DEX_FUTURE_SET_FLAGS_NONE = 0,

  /* Propagate first resolve/reject (use extra flags to specify) */
  DEX_FUTURE_SET_FLAGS_PROPAGATE_FIRST = 1 << 0,

  /* with PROPAGATE_FIRST, propagates on first resolve */
  DEX_FUTURE_SET_FLAGS_PROPAGATE_RESOLVE = 1 << 1,

  /* with PROPAGATE_FIRST, propagates on first reject */
  DEX_FUTURE_SET_FLAGS_PROPAGATE_REJECT = 1 << 2,
} DexFutureSetFlags;

DexFutureSet *dex_future_set_new (DexFuture         **futures,
                                  guint               n_futures,
                                  DexFutureSetFlags   flags);

#define DEX_TYPE_BLOCK    (dex_block_get_type())
#define DEX_BLOCK(obj)    (G_TYPE_CHECK_INSTANCE_CAST(obj, DEX_TYPE_BLOCK, DexBlock))
#define DEX_IS_BLOCK(obj) (G_TYPE_CHECK_INSTANCE_TYPE(obj, DEX_TYPE_BLOCK))

typedef enum _DexBlockKind
{
  DEX_BLOCK_KIND_THEN    = 1 << 0,
  DEX_BLOCK_KIND_CATCH   = 1 << 1,
  DEX_BLOCK_KIND_FINALLY = DEX_BLOCK_KIND_THEN | DEX_BLOCK_KIND_CATCH,
} DexBlockKind;

GType      dex_block_get_type (void) G_GNUC_CONST;
DexFuture *dex_block_new      (DexFuture         *future,
                               DexBlockKind       kind,
                               DexFutureCallback  callback,
                               gpointer           callback_data,
                               GDestroyNotify     callback_data_destroy);

typedef struct _DexPromise
{
  DexFuture parent_instance;
} DexPromise;

typedef struct _DexPromiseClass
{
  DexFutureClass parent_class;
} DexPromiseClass;

typedef struct _DexTasklet
{
  DexFuture parent_instance;
} DexTasklet;

typedef struct _DexTaskletClass
{
  DexFutureClass parent_class;
} DexTaskletClass;

typedef struct _DexScheduler
{
  DexObject parent_instance;
} DexScheduler;

typedef struct _DexSchedulerClass
{
  DexObjectClass parent_class;
} DexSchedulerClass;

G_END_DECLS
