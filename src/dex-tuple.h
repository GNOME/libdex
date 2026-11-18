/* dex-tuple.h
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

#include <glib-object.h>
#include "dex-version-macros.h"

G_BEGIN_DECLS

typedef struct _DexTuple DexTuple;

#define DEX_TYPE_TUPLE (dex_tuple_get_type())

DEX_AVAILABLE_IN_1_3
guint         dex_tuple_get_size     (DexTuple     *self);
DEX_AVAILABLE_IN_1_3
GType         dex_tuple_get_type     (void);
DEX_AVAILABLE_IN_1_3
const GValue *dex_tuple_get_value_at (DexTuple     *self,
                                      guint         position);
DEX_AVAILABLE_IN_1_3
DexTuple     *dex_tuple_newv         (const GValue *values,
                                      guint         n_values);
DEX_AVAILABLE_IN_1_3
DexTuple     *dex_tuple_ref          (DexTuple     *self);
DEX_AVAILABLE_IN_1_3
void          dex_tuple_unref        (DexTuple     *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (DexTuple, dex_tuple_unref)

G_END_DECLS
