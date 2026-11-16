/* dex-fiber.h
 *
 * Copyright 2022 Christian Hergert <christian@sourceandstack.com>
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
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "dex-future.h"

G_BEGIN_DECLS

#define DEX_TYPE_FIBER    (dex_fiber_get_type())
#define DEX_FIBER(obj)    (G_TYPE_CHECK_INSTANCE_CAST(obj, DEX_TYPE_FIBER, DexFiber))
#define DEX_IS_FIBER(obj) (G_TYPE_CHECK_INSTANCE_TYPE(obj, DEX_TYPE_FIBER))

typedef struct _DexFiber DexFiber;

DEX_AVAILABLE_IN_ALL
GType    dex_fiber_get_type      (void);
DEX_AVAILABLE_IN_ALL
gboolean dex_fiber_yield         (GError       **error) G_GNUC_WARN_UNUSED_RESULT;

DEX_AVAILABLE_IN_1_3
gboolean dex_fiber_yield_boolean (gboolean       value,
                                  GError       **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean dex_fiber_yield_boxed   (GType          expected_type,
                                  gpointer       value,
                                  GError       **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean dex_fiber_yield_double  (double         value,
                                  GError       **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean dex_fiber_yield_enum    (GType          expected_type,
                                  int            value,
                                  GError       **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean dex_fiber_yield_fd      (int            value,
                                  GError       **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean dex_fiber_yield_flags   (GType          expected_type,
                                  guint          value,
                                  GError       **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean dex_fiber_yield_float   (float          value,
                                  GError       **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean dex_fiber_yield_future  (DexFuture     *value,
                                  GError       **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean dex_fiber_yield_int     (int            value,
                                  GError       **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean dex_fiber_yield_int64   (gint64         value,
                                  GError       **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean dex_fiber_yield_object  (GObject       *value,
                                  GError       **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean dex_fiber_yield_pointer (gpointer       value,
                                  GError       **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean dex_fiber_yield_string  (const char    *value,
                                  GError       **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean dex_fiber_yield_uint    (guint          value,
                                  GError       **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean dex_fiber_yield_uint64  (guint64        value,
                                  GError       **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean dex_fiber_yield_value   (const GValue  *value,
                                  GError       **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean dex_fiber_yield_variant (GVariant      *value,
                                  GError       **error) G_GNUC_WARN_UNUSED_RESULT;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (DexFiber, dex_unref)

G_END_DECLS
