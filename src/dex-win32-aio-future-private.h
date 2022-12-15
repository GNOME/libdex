/* dex-win32-aio-future-private.h
 *
 * Copyright 2022 Christian Hergert <chergert@redhat.com>
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

#include "dex-aio-backend-private.h"

G_BEGIN_DECLS

#define DEX_TYPE_WIN32_AIO_FUTURE    (dex_win32_aio_future_get_type())
#define DEX_WIN32_AIO_FUTURE(obj)    (G_TYPE_CHECK_INSTANCE_CAST(obj, DEX_TYPE_WIN32_AIO_FUTURE, DexWin32AioFuture))
#define DEX_IS_WIN32_AIO_FUTURE(obj) (G_TYPE_CHECK_INSTANCE_TYPE(obj, DEX_TYPE_WIN32_AIO_FUTURE))

GType               dex_win32_aio_future_get_type         (void) G_GNUC_CONST;
DexWin32AioFuture  *dex_win32_aio_future_new_read         (DexWin32AioContext  *win32_aio_context,
                                                           int                  fd,
                                                           gpointer             buffer,
                                                           gsize                count,
                                                           goffset              offset);
DexWin32AioFuture  *dex_win32_aio_future_new_write        (DexWin32AioContext  *win32_aio_context,
                                                           int                  fd,
                                                           gconstpointer        buffer,
                                                           gsize                count,
                                                           goffset              offset);
void                dex_win32_aio_future_complete         (DexWin32AioFuture   *win32_aio_future);
DexWin32AioContext *dex_win32_aio_future_get_aio_context  (DexWin32AioFuture *win32_aio_future);
GMainContext       *dex_win32_aio_future_get_main_context (DexWin32AioFuture *win32_aio_future);

G_END_DECLS
