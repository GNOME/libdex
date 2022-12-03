/*
 * dex-future.h
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

#include "dex-enums.h"
#include "dex-object.h"

G_BEGIN_DECLS

#define DEX_TYPE_FUTURE       (dex_future_get_type())
#define DEX_FUTURE(object)    (G_TYPE_CHECK_INSTANCE_CAST(object, DEX_TYPE_FUTURE, DexFuture))
#define DEX_IS_FUTURE(object) (G_TYPE_CHECK_INSTANCE_TYPE(object, DEX_TYPE_FUTURE))

typedef struct _DexFuture DexFuture;

typedef DexFuture *(*DexFutureCallback) (DexFuture *future,
                                         gpointer   user_data);

DEX_AVAILABLE_IN_ALL
GType            dex_future_get_type        (void) G_GNUC_CONST;
DEX_AVAILABLE_IN_ALL
DexFutureStatus  dex_future_get_status      (DexFuture          *future);
DEX_AVAILABLE_IN_ALL
const GValue    *dex_future_get_value       (DexFuture          *future,
                                             GError            **error);
DEX_AVAILABLE_IN_ALL
DexFuture       *dex_future_then            (DexFuture          *future,
                                             DexFutureCallback   callback,
                                             gpointer            callback_data,
                                             GDestroyNotify      callback_data_destroy);
DEX_AVAILABLE_IN_ALL
DexFuture       *dex_future_then_loop       (DexFuture          *future,
                                             DexFutureCallback   callback,
                                             gpointer            callback_data,
                                             GDestroyNotify      callback_data_destroy);
DEX_AVAILABLE_IN_ALL
DexFuture       *dex_future_catch           (DexFuture          *future,
                                             DexFutureCallback   callback,
                                             gpointer            callback_data,
                                             GDestroyNotify      callback_data_destroy);
DEX_AVAILABLE_IN_ALL
DexFuture       *dex_future_catch_loop      (DexFuture          *future,
                                             DexFutureCallback   callback,
                                             gpointer            callback_data,
                                             GDestroyNotify      callback_data_destroy);
DEX_AVAILABLE_IN_ALL
DexFuture       *dex_future_finally         (DexFuture          *future,
                                             DexFutureCallback   callback,
                                             gpointer            callback_data,
                                             GDestroyNotify      callback_data_destroy);
DEX_AVAILABLE_IN_ALL
DexFuture       *dex_future_finally_loop    (DexFuture          *future,
                                             DexFutureCallback   callback,
                                             gpointer            callback_data,
                                             GDestroyNotify      callback_data_destroy);
DEX_AVAILABLE_IN_ALL
DexFuture       *dex_future_all             (DexFuture          *first_future,
                                             ...) G_GNUC_NULL_TERMINATED;
DEX_AVAILABLE_IN_ALL
DexFuture       *dex_future_allv            (DexFuture * const  *futures,
                                             guint               n_futures);
DEX_AVAILABLE_IN_ALL
DexFuture       *dex_future_all_race        (DexFuture          *first_future,
                                             ...) G_GNUC_NULL_TERMINATED;
DEX_AVAILABLE_IN_ALL
DexFuture       *dex_future_all_racev       (DexFuture * const  *futures,
                                             guint               n_futures);
DEX_AVAILABLE_IN_ALL
DexFuture       *dex_future_any             (DexFuture          *first_future,
                                             ...) G_GNUC_NULL_TERMINATED;
DEX_AVAILABLE_IN_ALL
DexFuture       *dex_future_anyv            (DexFuture * const  *futures,
                                             guint               n_futures);
DEX_AVAILABLE_IN_ALL
DexFuture       *dex_future_first           (DexFuture          *first_future,
                                             ...) G_GNUC_NULL_TERMINATED;
DEX_AVAILABLE_IN_ALL
DexFuture       *dex_future_firstv          (DexFuture  * const *futures,
                                             guint               n_futures);
DEX_AVAILABLE_IN_ALL
const char      *dex_future_get_name        (DexFuture          *future);
DEX_AVAILABLE_IN_ALL
void             dex_future_set_static_name (DexFuture          *future,
                                             const char         *name);

#if G_GNUC_CHECK_VERSION(3,0)
# define _DEX_FUTURE_NEW_(func, counter, ...) \
  ({ DexFuture *G_PASTE(__f, counter) = G_PASTE (dex_future_, func) (__VA_ARGS__); \
     dex_future_set_static_name (DEX_FUTURE (G_PASTE (__f, counter)), G_STRLOC); \
     G_PASTE (__f, counter); })
# define _DEX_FUTURE_NEW(func, ...) _DEX_FUTURE_NEW_(func, __COUNTER__, __VA_ARGS__)
# define dex_future_then(...) _DEX_FUTURE_NEW(then, __VA_ARGS__)
# define dex_future_then_loop(...) _DEX_FUTURE_NEW(then_loop, __VA_ARGS__)
# define dex_future_catch(...) _DEX_FUTURE_NEW(catch, __VA_ARGS__)
# define dex_future_catch_loop(...) _DEX_FUTURE_NEW(catch_loop, __VA_ARGS__)
# define dex_future_finally(...) _DEX_FUTURE_NEW(finally, __VA_ARGS__)
# define dex_future_finally_loop(...) _DEX_FUTURE_NEW(finally_loop, __VA_ARGS__)
# define dex_future_all(...) _DEX_FUTURE_NEW(all, __VA_ARGS__)
# define dex_future_allv(...) _DEX_FUTURE_NEW(allv, __VA_ARGS__)
# define dex_future_all_race(...) _DEX_FUTURE_NEW(all_race, __VA_ARGS__)
# define dex_future_all_racev(...) _DEX_FUTURE_NEW(all_racev, __VA_ARGS__)
# define dex_future_any(...) _DEX_FUTURE_NEW(any, __VA_ARGS__)
# define dex_future_anyv(...) _DEX_FUTURE_NEW(anyv, __VA_ARGS__)
# define dex_future_first(...) _DEX_FUTURE_NEW(first, __VA_ARGS__)
# define dex_future_firstv(...) _DEX_FUTURE_NEW(firstv, __VA_ARGS__)
#endif

G_DEFINE_AUTOPTR_CLEANUP_FUNC (DexFuture, dex_unref)

G_END_DECLS
