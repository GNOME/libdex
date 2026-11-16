/*
 * dex-coroutine.h
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#if !defined (DEX_INSIDE) && !defined (DEX_COMPILATION)
# error "Only <libdex.h> can be included directly."
#endif

#include "dex-future.h"
#include "dex-scheduler.h"
#include "dex-version-macros.h"

G_BEGIN_DECLS

typedef struct _DexCoroutine      DexCoroutine;
typedef struct _DexCoroutineClass DexCoroutineClass;

#define DEX_TYPE_COROUTINE    (dex_coroutine_get_type())
#define DEX_COROUTINE(obj)    (G_TYPE_CHECK_INSTANCE_CAST ((obj), DEX_TYPE_COROUTINE, DexCoroutine))
#define DEX_IS_COROUTINE(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DEX_TYPE_COROUTINE))

DEX_AVAILABLE_IN_ALL
void  dex_coroutine_context_resume  (DexCoroutineContext  *context,
                                     guint                *pc,
                                     DexFuture           **future);
DEX_AVAILABLE_IN_ALL
void  dex_coroutine_context_suspend (DexCoroutineContext  *context,
                                     guint                 pc,
                                     DexFuture            *future);
DEX_AVAILABLE_IN_ALL
GType dex_coroutine_get_type        (void);

/**
 * DEX_COROUTINE_BEGIN:
 *
 * Used in a coroutine function body to establish coroutine state.
 *
 * The `context` argument is expected to be a [struct@Dex.CoroutineContext].
 *
 * Coroutine functions are expected to return #DexFuture for completion, and
 * return %NULL while suspended. When suspended, `pending` contains the awaited
 * future and is owned by the coroutine state.
 */
#define DEX_COROUTINE_BEGIN(context)                                              \
{ DexCoroutineContext *__dex_coro = (DexCoroutineContext *)(context);             \
  guint __dex_coro_pc = 0;                                                        \
  DexFuture *__dex_coro_pending = NULL;                                           \
  g_return_val_if_fail (__dex_coro != NULL, NULL);                                \
  dex_coroutine_context_resume (__dex_coro, &__dex_coro_pc, &__dex_coro_pending); \
  switch (__dex_coro_pc) { case 0:

/**
 * DEX_COROUTINE_END:
 *
 * Closes a #DEX_COROUTINE_BEGIN block.
 */
#define DEX_COROUTINE_END    \
  default:                   \
    g_assert_not_reached (); \
    return NULL;             \
  }                          \
  return NULL; }

#define _DEX_COROUTINE_SUSPEND(_out, _error, _future, _await, _id)                        \
  G_GNUC_FALLTHROUGH;                                                                     \
  case _id:                                                                               \
    {                                                                                     \
      gboolean __resumed = __dex_coro_pc == (_id);                                        \
      DexFuture *__future = __resumed ? __dex_coro_pending : (_future);                   \
      if (dex_future_is_pending (__future))                                               \
        {                                                                                 \
          dex_coroutine_context_suspend (__dex_coro, (_id), g_steal_pointer (&__future)); \
          return NULL;                                                                    \
        }                                                                                 \
      __dex_coro_pc = 0;                                                                  \
      *(_out) = (__typeof__(*(_out))) (_await (g_steal_pointer (&__future), (_error)));   \
  }

#define _DEX_COROUTINE_SUSPEND_VOID(_error, _future, _id)                                 \
  G_GNUC_FALLTHROUGH;                                                                     \
  case _id:                                                                               \
    {                                                                                     \
      gboolean __resumed = __dex_coro_pc == (_id);                                        \
      DexFuture *__future = __resumed ? __dex_coro_pending : (_future);                   \
      if (dex_future_is_pending (__future))                                               \
        {                                                                                 \
          dex_coroutine_context_suspend (__dex_coro, (_id), g_steal_pointer (&__future)); \
          return NULL;                                                                    \
        }                                                                                 \
      __dex_coro_pc = 0;                                                                  \
      (void) dex_await (g_steal_pointer (&__future), (_error));                           \
  }

#define _DEX_COROUTINE_LABEL_ID() (__COUNTER__ + 1)

#define DEX_COROUTINE_SUSPEND(_future, _error) \
  _DEX_COROUTINE_SUSPEND_VOID (_error, _future, _DEX_COROUTINE_LABEL_ID ())

#define DEX_COROUTINE_SUSPEND_BOOLEAN(_out, _error, _future) \
  _DEX_COROUTINE_SUSPEND (_out, _error, _future, dex_await_boolean, _DEX_COROUTINE_LABEL_ID ())

#define DEX_COROUTINE_SUSPEND_ENUM(_out, _error, _future) \
  _DEX_COROUTINE_SUSPEND (_out, _error, _future, dex_await_enum, _DEX_COROUTINE_LABEL_ID ())

#define DEX_COROUTINE_SUSPEND_FLAGS(_out, _error, _future) \
  _DEX_COROUTINE_SUSPEND (_out, _error, _future, dex_await_flags, _DEX_COROUTINE_LABEL_ID ())

#define DEX_COROUTINE_SUSPEND_OBJECT(_out, _error, _future) \
  _DEX_COROUTINE_SUSPEND (_out, _error, _future, dex_await_object, _DEX_COROUTINE_LABEL_ID ())

#define DEX_COROUTINE_SUSPEND_BOXED(_out, _error, _future) \
  _DEX_COROUTINE_SUSPEND (_out, _error, _future, dex_await_boxed, _DEX_COROUTINE_LABEL_ID ())

#define DEX_COROUTINE_SUSPEND_POINTER(_out, _error, _future) \
  _DEX_COROUTINE_SUSPEND (_out, _error, _future, dex_await_pointer, _DEX_COROUTINE_LABEL_ID ())

#define DEX_COROUTINE_SUSPEND_FD(_out, _error, _future) \
  _DEX_COROUTINE_SUSPEND (_out, _error, _future, dex_await_fd, _DEX_COROUTINE_LABEL_ID ())

#define DEX_COROUTINE_SUSPEND_INT(_out, _error, _future) \
  _DEX_COROUTINE_SUSPEND (_out, _error, _future, dex_await_int, _DEX_COROUTINE_LABEL_ID ())

#define DEX_COROUTINE_SUSPEND_UINT(_out, _error, _future) \
  _DEX_COROUTINE_SUSPEND (_out, _error, _future, dex_await_uint, _DEX_COROUTINE_LABEL_ID ())

#define DEX_COROUTINE_SUSPEND_INT64(_out, _error, _future) \
  _DEX_COROUTINE_SUSPEND (_out, _error, _future, dex_await_int64, _DEX_COROUTINE_LABEL_ID ())

#define DEX_COROUTINE_SUSPEND_UINT64(_out, _error, _future) \
  _DEX_COROUTINE_SUSPEND (_out, _error, _future, dex_await_uint64, _DEX_COROUTINE_LABEL_ID ())

#define DEX_COROUTINE_SUSPEND_DOUBLE(_out, _error, _future) \
  _DEX_COROUTINE_SUSPEND (_out, _error, _future, dex_await_double, _DEX_COROUTINE_LABEL_ID ())

#define DEX_COROUTINE_SUSPEND_FLOAT(_out, _error, _future) \
  _DEX_COROUTINE_SUSPEND (_out, _error, _future, dex_await_float, _DEX_COROUTINE_LABEL_ID ())

DEX_AVAILABLE_IN_1_3
DexFuture *dex_coroutine_context_yield_boolean (DexCoroutineContext *context,
                                                gboolean             value) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexFuture *dex_coroutine_context_yield_boxed   (DexCoroutineContext *context,
                                                GType                expected_type,
                                                gpointer             value) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexFuture *dex_coroutine_context_yield_double  (DexCoroutineContext *context,
                                                double               value) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexFuture *dex_coroutine_context_yield_enum    (DexCoroutineContext *context,
                                                GType                expected_type,
                                                int                  value) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexFuture *dex_coroutine_context_yield_fd      (DexCoroutineContext *context,
                                                int                  value) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexFuture *dex_coroutine_context_yield_flags   (DexCoroutineContext *context,
                                                GType                expected_type,
                                                guint                value) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexFuture *dex_coroutine_context_yield_float   (DexCoroutineContext *context,
                                                float                value) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexFuture *dex_coroutine_context_yield_future  (DexCoroutineContext *context,
                                                DexFuture           *value) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexFuture *dex_coroutine_context_yield_int     (DexCoroutineContext *context,
                                                int                  value) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexFuture *dex_coroutine_context_yield_int64   (DexCoroutineContext *context,
                                                gint64               value) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexFuture *dex_coroutine_context_yield_object  (DexCoroutineContext *context,
                                                GObject             *value) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexFuture *dex_coroutine_context_yield_pointer (DexCoroutineContext *context,
                                                gpointer             value) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexFuture *dex_coroutine_context_yield_string  (DexCoroutineContext *context,
                                                const char          *value) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexFuture *dex_coroutine_context_yield_uint    (DexCoroutineContext *context,
                                                guint                value) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexFuture *dex_coroutine_context_yield_uint64  (DexCoroutineContext *context,
                                                guint64              value) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexFuture *dex_coroutine_context_yield_value   (DexCoroutineContext *context,
                                                const GValue        *value) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexFuture *dex_coroutine_context_yield_variant (DexCoroutineContext *context,
                                                GVariant            *value) G_GNUC_WARN_UNUSED_RESULT;

/**
 * DEX_COROUTINE_YIELD_VALUE:
 * @value: expression evaluated once for this yield
 * @error: return location for an error
 *
 * Yields a copy of @value and suspends until the next pull. Keep state in
 * user data across yields, and return from the producer on error.
 *
 * Since: 1.3
 */
#define DEX_COROUTINE_YIELD_VALUE(value, error) \
  DEX_COROUTINE_SUSPEND (dex_coroutine_context_yield_value (__dex_coro, (value)), (error))

/**
 * DEX_COROUTINE_YIELD_BOOLEAN:
 * @value: expression evaluated once for this yield
 * @error: return location for an error
 *
 * Like DEX_COROUTINE_YIELD_VALUE(), for boolean values.
 *
 * Since: 1.3
 */
#define DEX_COROUTINE_YIELD_BOOLEAN(value, error) \
  DEX_COROUTINE_SUSPEND (dex_coroutine_context_yield_boolean (__dex_coro, (value)), (error))

/**
 * DEX_COROUTINE_YIELD_INT:
 * @value: expression evaluated once for this yield
 * @error: return location for an error
 *
 * Like DEX_COROUTINE_YIELD_VALUE(), for int values.
 *
 * Since: 1.3
 */
#define DEX_COROUTINE_YIELD_INT(value, error) \
  DEX_COROUTINE_SUSPEND (dex_coroutine_context_yield_int (__dex_coro, (value)), (error))

/**
 * DEX_COROUTINE_YIELD_UINT:
 * @value: expression evaluated once for this yield
 * @error: return location for an error
 *
 * Like DEX_COROUTINE_YIELD_VALUE(), for uint values.
 *
 * Since: 1.3
 */
#define DEX_COROUTINE_YIELD_UINT(value, error) \
  DEX_COROUTINE_SUSPEND (dex_coroutine_context_yield_uint (__dex_coro, (value)), (error))

/**
 * DEX_COROUTINE_YIELD_INT64:
 * @value: expression evaluated once for this yield
 * @error: return location for an error
 *
 * Like DEX_COROUTINE_YIELD_VALUE(), for int64 values.
 *
 * Since: 1.3
 */
#define DEX_COROUTINE_YIELD_INT64(value, error) \
  DEX_COROUTINE_SUSPEND (dex_coroutine_context_yield_int64 (__dex_coro, (value)), (error))

/**
 * DEX_COROUTINE_YIELD_UINT64:
 * @value: expression evaluated once for this yield
 * @error: return location for an error
 *
 * Like DEX_COROUTINE_YIELD_VALUE(), for uint64 values.
 *
 * Since: 1.3
 */
#define DEX_COROUTINE_YIELD_UINT64(value, error) \
  DEX_COROUTINE_SUSPEND (dex_coroutine_context_yield_uint64 (__dex_coro, (value)), (error))

/**
 * DEX_COROUTINE_YIELD_FLOAT:
 * @value: expression evaluated once for this yield
 * @error: return location for an error
 *
 * Like DEX_COROUTINE_YIELD_VALUE(), for float values.
 *
 * Since: 1.3
 */
#define DEX_COROUTINE_YIELD_FLOAT(value, error) \
  DEX_COROUTINE_SUSPEND (dex_coroutine_context_yield_float (__dex_coro, (value)), (error))

/**
 * DEX_COROUTINE_YIELD_DOUBLE:
 * @value: expression evaluated once for this yield
 * @error: return location for an error
 *
 * Like DEX_COROUTINE_YIELD_VALUE(), for double values.
 *
 * Since: 1.3
 */
#define DEX_COROUTINE_YIELD_DOUBLE(value, error) \
  DEX_COROUTINE_SUSPEND (dex_coroutine_context_yield_double (__dex_coro, (value)), (error))

/**
 * DEX_COROUTINE_YIELD_ENUM:
 * @type: expected value type
 * @value: expression evaluated once for this yield
 * @error: return location for an error
 *
 * Like DEX_COROUTINE_YIELD_VALUE(), for enum values.
 *
 * Since: 1.3
 */
#define DEX_COROUTINE_YIELD_ENUM(type, value, error) \
  DEX_COROUTINE_SUSPEND (dex_coroutine_context_yield_enum (__dex_coro, (type), (value)), (error))

/**
 * DEX_COROUTINE_YIELD_FLAGS:
 * @type: expected value type
 * @value: expression evaluated once for this yield
 * @error: return location for an error
 *
 * Like DEX_COROUTINE_YIELD_VALUE(), for flags values.
 *
 * Since: 1.3
 */
#define DEX_COROUTINE_YIELD_FLAGS(type, value, error) \
  DEX_COROUTINE_SUSPEND (dex_coroutine_context_yield_flags (__dex_coro, (type), (value)), (error))

/**
 * DEX_COROUTINE_YIELD_STRING:
 * @value: expression evaluated once for this yield
 * @error: return location for an error
 *
 * Like DEX_COROUTINE_YIELD_VALUE(), for string values.
 *
 * Since: 1.3
 */
#define DEX_COROUTINE_YIELD_STRING(value, error) \
  DEX_COROUTINE_SUSPEND (dex_coroutine_context_yield_string (__dex_coro, (value)), (error))

/**
 * DEX_COROUTINE_YIELD_VARIANT:
 * @value: expression evaluated once for this yield
 * @error: return location for an error
 *
 * Like DEX_COROUTINE_YIELD_VALUE(), for variant values.
 *
 * Since: 1.3
 */
#define DEX_COROUTINE_YIELD_VARIANT(value, error) \
  DEX_COROUTINE_SUSPEND (dex_coroutine_context_yield_variant (__dex_coro, (value)), (error))

/**
 * DEX_COROUTINE_YIELD_BOXED:
 * @type: expected value type
 * @value: expression evaluated once for this yield
 * @error: return location for an error
 *
 * Like DEX_COROUTINE_YIELD_VALUE(), for boxed values.
 *
 * Since: 1.3
 */
#define DEX_COROUTINE_YIELD_BOXED(type, value, error) \
  DEX_COROUTINE_SUSPEND (dex_coroutine_context_yield_boxed (__dex_coro, (type), (value)), (error))

/**
 * DEX_COROUTINE_YIELD_POINTER:
 * @value: expression evaluated once for this yield
 * @error: return location for an error
 *
 * Like DEX_COROUTINE_YIELD_VALUE(), for pointer values.
 *
 * Since: 1.3
 */
#define DEX_COROUTINE_YIELD_POINTER(value, error) \
  DEX_COROUTINE_SUSPEND (dex_coroutine_context_yield_pointer (__dex_coro, (value)), (error))

/**
 * DEX_COROUTINE_YIELD_OBJECT:
 * @value: expression evaluated once for this yield
 * @error: return location for an error
 *
 * Like DEX_COROUTINE_YIELD_VALUE(), for object values.
 *
 * Since: 1.3
 */
#define DEX_COROUTINE_YIELD_OBJECT(value, error) \
  DEX_COROUTINE_SUSPEND (dex_coroutine_context_yield_object (__dex_coro, (value)), (error))

/**
 * DEX_COROUTINE_YIELD_FUTURE:
 * @value: expression evaluated once for this yield
 * @error: return location for an error
 *
 * Like DEX_COROUTINE_YIELD_VALUE(), for future values.
 *
 * Since: 1.3
 */
#define DEX_COROUTINE_YIELD_FUTURE(value, error) \
  DEX_COROUTINE_SUSPEND (dex_coroutine_context_yield_future (__dex_coro, (value)), (error))

/**
 * DEX_COROUTINE_YIELD_FD:
 * @value: expression evaluated once for this yield
 * @error: return location for an error
 *
 * Like DEX_COROUTINE_YIELD_VALUE(), for fd values.
 *
 * Since: 1.3
 */
#define DEX_COROUTINE_YIELD_FD(value, error) \
  DEX_COROUTINE_SUSPEND (dex_coroutine_context_yield_fd (__dex_coro, (value)), (error))

G_END_DECLS
