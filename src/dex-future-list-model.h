/* dex-future-list-model.h
 *
 * Copyright 2025 Christian Hergert <chergert@redhat.com>
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

#include <libdex.h>

G_BEGIN_DECLS

#define DEX_TYPE_FUTURE_LIST_MODEL (dex_future_list_model_get_type())

DEX_AVAILABLE_IN_1_1
G_DECLARE_FINAL_TYPE (DexFutureListModel, dex_future_list_model, DEX, FUTURE_LIST_MODEL, GObject)

DEX_AVAILABLE_IN_1_1
GListModel *dex_future_list_model_new        (DexFuture          *future);
DEX_AVAILABLE_IN_1_1
DexFuture  *dex_future_list_model_dup_future (DexFutureListModel *self);

G_END_DECLS
