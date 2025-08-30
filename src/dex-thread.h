/* dex-thread.h
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

#include "dex-future.h"

G_BEGIN_DECLS

/**
 * DexThreadFunc:
 *
 * A function which will be run on a dedicated thread.
 *
 * It must return a [class@Dex.Future] that will eventually resolve
 * to a value or reject with error.
 *
 * Returns: (transfer full): a [class@Dex.Future]
 *
 * Since: 1.0
 */
typedef DexFuture *(*DexThreadFunc) (gpointer user_data);

DEX_AVAILABLE_IN_ALL
DexFuture *dex_thread_spawn    (const char      *thread_name,
                                DexThreadFunc    thread_func,
                                gpointer         user_data,
                                GDestroyNotify   user_data_destroy) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_ALL
gboolean   dex_thread_wait_for (DexFuture       *future,
                                GError         **error);

G_END_DECLS
