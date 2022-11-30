/* dex-kqueue-future-private.h
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

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

#include "dex-future.h"

G_BEGIN_DECLS

#define DEX_TYPE_KQUEUE_FUTURE    (dex_kqueue_future_get_type())
#define DEX_KQUEUE_FUTURE(obj)    (G_TYPE_CHECK_INSTANCE_CAST(obj, DEX_TYPE_KQUEUE_FUTURE, DexKqueueFuture))
#define DEX_IS_KQUEUE_FUTURE(obj) (G_TYPE_CHECK_INSTANCE_TYPE(obj, DEX_TYPE_KQUEUE_FUTURE))

typedef struct _DexKqueueFuture DexKqueueFuture;

GType            dex_kqueue_future_get_type  (void) G_GNUC_CONST;
DexKqueueFuture *dex_kqueue_future_new_read  (int                  fd,
                                              gpointer             buffer,
                                              gsize                count,
                                              goffset              offset);
DexKqueueFuture *dex_kqueue_future_new_write (int                  fd,
                                              gconstpointer        buffer,
                                              gsize                count,
                                              goffset              offset);
gboolean         dex_kqueue_future_submit    (DexKqueueFuture      *kqueue_future,
                                              int                   kqueue_fd);
void             dex_kqueue_future_complete  (DexKqueueFuture      *kqueue_future,
                                              const struct kevent  *event);

G_END_DECLS
