/*
 * dex-closure.h
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#if !defined (DEX_INSIDE) && !defined (DEX_COMPILATION)
# error "Only <libdex.h> can be included directly."
#endif

#include <glib-object.h>

#include "dex-future.h"
#include "dex-version-macros.h"

G_BEGIN_DECLS

/**
 * DEX_DEFINE_CLOSURE_VALUE:
 * @type: field type
 * @name: field name
 *
 * A value field descriptor for #DEX_DEFINE_CLOSURE_TYPE().
 */
#define DEX_DEFINE_CLOSURE_VALUE(type, name) \
  (_DEX_CLOSURE_FIELD_VALUE_DECLARE, _DEX_CLOSURE_FIELD_VALUE_CLEAR, type, name, NULL)

/**
 * DEX_DEFINE_CLOSURE_VALUE_WITH_CLEAR:
 * @type: field type
 * @name: field name
 * @clear: clear callback taking a pointer to @type
 *
 * A value field descriptor for #DEX_DEFINE_CLOSURE_TYPE() which is cleared
 * by calling @clear with the address of the value.
 */
#define DEX_DEFINE_CLOSURE_VALUE_WITH_CLEAR(type, name, clear) \
  (_DEX_CLOSURE_FIELD_VALUE_DECLARE, _DEX_CLOSURE_FIELD_VALUE_WITH_CLEAR_CLEAR, \
   type, name, clear)

/**
 * DEX_DEFINE_CLOSURE_POINTER:
 * @type: field type
 * @name: field name
 * @clear: clear callback for @type
 *
 * A pointer field descriptor for #DEX_DEFINE_CLOSURE_TYPE(). @clear is
 * required; use #DEX_DEFINE_CLOSURE_VALUE() for pointers which do not need
 * cleanup.
 */
#define DEX_DEFINE_CLOSURE_POINTER(type, name, clear) \
  (_DEX_CLOSURE_FIELD_POINTER_DECLARE, _DEX_CLOSURE_FIELD_POINTER_CLEAR, type, name, clear)

/**
 * DEX_DEFINE_CLOSURE_OBJECT:
 * @type: object type
 * @name: field name
 *
 * An object pointer field descriptor for #DEX_DEFINE_CLOSURE_TYPE().
 */
#define DEX_DEFINE_CLOSURE_OBJECT(type, name) \
  (_DEX_CLOSURE_FIELD_OBJECT_DECLARE, _DEX_CLOSURE_FIELD_OBJECT_CLEAR, type, name, NULL)

/**
 * DEX_DEFINE_CLOSURE_TYPE:
 * @type_name: generated struct name
 * @type_prefix: function prefix for generated helpers
 * @...: one or more `DEX_DEFINE_CLOSURE_*()` field descriptors
 *
 * This macro is a lightweight helper for quickly defining closure-like state
 * structs with matching `type_prefix_new()` and `type_prefix_free()` helpers.
 *
 * It is intentionally generic; while coroutine task states are a common use case,
 * the generated type is also useful for quick one-off task/closure structs.
 *
 * Example:
 * ```c
 * DEX_DEFINE_CLOSURE_TYPE (MyTaskState, my_task_state,
 *                          DEX_DEFINE_CLOSURE_VALUE (gsize, bytes),
 *                          DEX_DEFINE_CLOSURE_POINTER (GBytes *, bytes_obj, g_bytes_unref),
 *                          DEX_DEFINE_CLOSURE_OBJECT (GSocketConnection, conn))
 * ```
 *
 * Since: 1.2
 */
#define DEX_DEFINE_CLOSURE_TYPE(type_name, type_prefix, ...)        \
  typedef struct _##type_name                                       \
  {                                                                 \
    guint      pc;                                                  \
    DexFuture *pending;                                             \
    _DEX_CLOSURE_FOR_EACH (_DEX_CLOSURE_FIELD_DECLARE, __VA_ARGS__) \
  } type_name;                                                      \
  static inline type_name *type_prefix##_new (void)                 \
  {                                                                 \
    return g_new0 (type_name, 1);                                   \
  }                                                                 \
  static inline void type_prefix##_free (type_name *state)          \
  {                                                                 \
    if (state == NULL)                                              \
      return;                                                       \
    dex_clear ((gpointer) &state->pending);                         \
    _DEX_CLOSURE_FOR_EACH (_DEX_CLOSURE_FIELD_CLEAR, __VA_ARGS__)   \
    g_free (state);                                                 \
  }

#define _DEX_CLOSURE_FIELD_DECLARE(field) \
  _DEX_CLOSURE_FIELD_DECLARE_1 field
#define _DEX_CLOSURE_FIELD_DECLARE_1(declare, clear_func, type, name, clear) \
  declare (type, name, clear)

#define _DEX_CLOSURE_FIELD_VALUE_DECLARE(type, name, clear) type name;
#define _DEX_CLOSURE_FIELD_POINTER_DECLARE(type, name, clear) type name;
#define _DEX_CLOSURE_FIELD_OBJECT_DECLARE(type, name, clear) type *name;

#define _DEX_CLOSURE_FIELD_CLEAR(field) \
  _DEX_CLOSURE_FIELD_CLEAR_1 field
#define _DEX_CLOSURE_FIELD_CLEAR_1(declare, clear_func, type, name, clear) \
  clear_func (state, type, name, clear);

#define _DEX_CLOSURE_FIELD_VALUE_CLEAR(state, type, name, clear) \
  G_STMT_START { } G_STMT_END

#define _DEX_CLOSURE_FIELD_VALUE_WITH_CLEAR_CLEAR(state, type, name, clear) \
  G_STMT_START {                                                            \
    (clear) (&(state)->name);                                               \
  } G_STMT_END

#define _DEX_CLOSURE_FIELD_POINTER_CLEAR(state, type, name, clear) \
  G_STMT_START {                                                   \
    g_clear_pointer (&(state)->name, clear);                       \
  } G_STMT_END

#define _DEX_CLOSURE_FIELD_OBJECT_CLEAR(state, type, name, clear) \
  G_STMT_START {                                                  \
    g_clear_object ((GObject **) &((state)->name));               \
  } G_STMT_END

#define _DEX_CLOSURE_FOR_EACH(macro, ...) \
  _DEX_CLOSURE_CONCAT (_DEX_CLOSURE_FOR_EACH_, _DEX_CLOSURE_NARGS (__VA_ARGS__)) \
    (macro, __VA_ARGS__)

#define _DEX_CLOSURE_CONCAT(a, b) \
  _DEX_CLOSURE_CONCAT_EXPAND (a, b)
#define _DEX_CLOSURE_CONCAT_EXPAND(a, b) a##b

#define _DEX_CLOSURE_NARGS(...) \
  _DEX_CLOSURE_NARGS_ (__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define _DEX_CLOSURE_NARGS_(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, ...) a17

#define _DEX_CLOSURE_FOR_EACH_0(macro)
#define _DEX_CLOSURE_FOR_EACH_1(macro, a1) \
  macro(a1)
#define _DEX_CLOSURE_FOR_EACH_2(macro, a1, a2) \
  macro(a1) macro(a2)
#define _DEX_CLOSURE_FOR_EACH_3(macro, a1, a2, a3) \
  macro(a1) macro(a2) macro(a3)
#define _DEX_CLOSURE_FOR_EACH_4(macro, a1, a2, a3, a4) \
  macro(a1) macro(a2) macro(a3) macro(a4)
#define _DEX_CLOSURE_FOR_EACH_5(macro, a1, a2, a3, a4, a5) \
  macro(a1) macro(a2) macro(a3) macro(a4) macro(a5)
#define _DEX_CLOSURE_FOR_EACH_6(macro, a1, a2, a3, a4, a5, a6) \
  macro(a1) macro(a2) macro(a3) macro(a4) macro(a5) macro(a6)
#define _DEX_CLOSURE_FOR_EACH_7(macro, a1, a2, a3, a4, a5, a6, a7) \
  macro(a1) macro(a2) macro(a3) macro(a4) macro(a5) macro(a6) macro(a7)
#define _DEX_CLOSURE_FOR_EACH_8(macro, a1, a2, a3, a4, a5, a6, a7, a8) \
  macro(a1) macro(a2) macro(a3) macro(a4) macro(a5) macro(a6) macro(a7) macro(a8)
#define _DEX_CLOSURE_FOR_EACH_9(macro, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
  macro(a1) macro(a2) macro(a3) macro(a4) macro(a5) macro(a6) macro(a7) macro(a8) \
  macro(a9)
#define _DEX_CLOSURE_FOR_EACH_10(macro, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
  macro(a1) macro(a2) macro(a3) macro(a4) macro(a5) macro(a6) macro(a7) macro(a8) \
  macro(a9) macro(a10)
#define _DEX_CLOSURE_FOR_EACH_11(macro, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
  macro(a1) macro(a2) macro(a3) macro(a4) macro(a5) macro(a6) macro(a7) macro(a8)  \
  macro(a9) macro(a10) macro(a11)
#define _DEX_CLOSURE_FOR_EACH_12(macro, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
  macro(a1) macro(a2) macro(a3) macro(a4) macro(a5) macro(a6) macro(a7) macro(a8)  \
  macro(a9) macro(a10) macro(a11) macro(a12)
#define _DEX_CLOSURE_FOR_EACH_13(macro, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
  macro(a1) macro(a2) macro(a3) macro(a4) macro(a5) macro(a6) macro(a7) macro(a8)  \
  macro(a9) macro(a10) macro(a11) macro(a12) macro(a13)
#define _DEX_CLOSURE_FOR_EACH_14(macro, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
  macro(a1) macro(a2) macro(a3) macro(a4) macro(a5) macro(a6) macro(a7) macro(a8)  \
  macro(a9) macro(a10) macro(a11) macro(a12) macro(a13) macro(a14)
#define _DEX_CLOSURE_FOR_EACH_15(macro, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
  macro(a1) macro(a2) macro(a3) macro(a4) macro(a5) macro(a6) macro(a7) macro(a8)  \
  macro(a9) macro(a10) macro(a11) macro(a12) macro(a13) macro(a14) macro(a15)
#define _DEX_CLOSURE_FOR_EACH_16(macro, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16) \
  macro(a1) macro(a2) macro(a3) macro(a4) macro(a5) macro(a6) macro(a7) macro(a8)  \
  macro(a9) macro(a10) macro(a11) macro(a12) macro(a13) macro(a14) macro(a15) macro(a16)

G_END_DECLS
