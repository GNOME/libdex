/*
 * dex-promise.h
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

#if !defined (DEX_INSIDE) && !defined (DEX_COMPILATION)
# error "Only <libdex.h> can be included directly."
#endif

#include "dex-future.h"

G_BEGIN_DECLS

#define DEX_TYPE_PROMISE    (dex_promise_get_type())
#define DEX_IS_PROMISE(obj) (G_TYPE_CHECK_INSTANCE_TYPE(obj, DEX_TYPE_PROMISE))

typedef struct _DexPromise DexPromise;

DEX_AVAILABLE_IN_ALL
GType       dex_promise_get_type        (void) G_GNUC_CONST;
DEX_AVAILABLE_IN_ALL
DexPromise *dex_promise_new             (void);
DEX_AVAILABLE_IN_ALL
DexPromise *dex_promise_new_for_error   (GError       *error);
DEX_AVAILABLE_IN_ALL
DexPromise *dex_promise_new_reject      (GQuark        domain,
                                         int           code,
                                         const char   *format,
                                         ...) G_GNUC_PRINTF (3, 4);
DEX_AVAILABLE_IN_ALL
DexPromise *dex_promise_new_for_value   (const GValue *value);
DEX_AVAILABLE_IN_ALL
DexPromise *dex_promise_new_for_string  (const char   *string);
DEX_AVAILABLE_IN_ALL
DexPromise *dex_promise_new_for_int     (int           v_int);
DEX_AVAILABLE_IN_ALL
DexPromise *dex_promise_new_for_boolean (gboolean      v_bool);
DEX_AVAILABLE_IN_ALL
void        dex_promise_resolve         (DexPromise   *promise,
                                         const GValue *value);
DEX_AVAILABLE_IN_ALL
void        dex_promise_resolve_int     (DexPromise   *promise,
                                         int           value);
DEX_AVAILABLE_IN_ALL
void        dex_promise_resolve_uint    (DexPromise   *promise,
                                         guint         value);
DEX_AVAILABLE_IN_ALL
void        dex_promise_resolve_int64   (DexPromise   *promise,
                                         gint64        value);
DEX_AVAILABLE_IN_ALL
void        dex_promise_resolve_uint64  (DexPromise   *promise,
                                         guint64       value);
DEX_AVAILABLE_IN_ALL
void        dex_promise_resolve_long    (DexPromise   *promise,
                                         glong         value);
DEX_AVAILABLE_IN_ALL
void        dex_promise_resolve_ulong   (DexPromise   *promise,
                                         glong         value);
DEX_AVAILABLE_IN_ALL
void        dex_promise_resolve_float   (DexPromise   *promise,
                                         float         value);
DEX_AVAILABLE_IN_ALL
void        dex_promise_resolve_double  (DexPromise   *promise,
                                         double        value);
DEX_AVAILABLE_IN_ALL
void        dex_promise_resolve_boolean (DexPromise   *promise,
                                         gboolean      value);
DEX_AVAILABLE_IN_ALL
void        dex_promise_resolve_string  (DexPromise   *promise,
                                         char         *value);
DEX_AVAILABLE_IN_ALL
void        dex_promise_resolve_object  (DexPromise   *promise,
                                         gpointer      object);
DEX_AVAILABLE_IN_ALL
void        dex_promise_reject          (DexPromise   *promise,
                                         GError       *error);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (DexPromise, dex_unref)

G_END_DECLS
