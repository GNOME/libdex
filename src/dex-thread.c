/* dex-thread.c
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

#include "config.h"

#include <gobject/gvaluecollector.h>

#include "dex-block-private.h"
#include "dex-promise.h"
#include "dex-thread.h"

typedef struct _DexTrampoline
{
  GCallback     callback;
  GArray       *values;
  DexPromise   *promise;
  GType         return_type;
} DexTrampoline;

static void
dex_trampoline_free (DexTrampoline *state)
{
  state->callback = NULL;
  g_clear_pointer (&state->values, g_array_unref);
  dex_clear (&state->promise);
  g_free (state);
}

static gpointer
dex_trampoline_thread (gpointer data)
{
  DexTrampoline *state = data;
  GClosure *c_closure = NULL;
  GValue retval = G_VALUE_INIT;

  g_assert (state != NULL);

  c_closure = g_cclosure_new (state->callback, NULL, NULL);
  g_closure_set_marshal (c_closure, g_cclosure_marshal_generic);

  g_value_init (&retval, state->return_type);
  g_closure_invoke (c_closure,
                    &retval,
                    state->values->len,
                    &g_array_index (state->values, GValue, 0),
                    NULL);

  dex_promise_resolve (state->promise, &retval);

  g_closure_unref (c_closure);
  g_value_unset (&retval);

  dex_trampoline_free (state);

  return NULL;
}

/**
 * dex_thread_spawn: (skip)
 * @name: (nullable): the name for the thread
 * @callback: the callback to execute
 * @return_type: a GType for the return type
 * @n_params: the number of (GType, value) pairs following
 * @...: pairs of `GType` followed by the value
 *
 * Spawns a new thread named @name running @callback.
 *
 * @return_value should be set to the GType of the return value of the
 * function. This will be propagated as the resolved value to the
 * [class@Dex.Future] returned from this function.
 *
 * @n_params should be the number of pairs of arguments following which
 * is in the order of (`GType`, `value`). These parameters will be passed
 * to @callback.
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to the
 *   value returned from @callback which must match @return_type.
 *
 * Since: 0.12
 */
DexFuture *
dex_thread_spawn (const char *name,
                  GCallback   callback,
                  GType       return_type,
                  guint       n_params,
                  ...)
{
  char *errmsg = NULL;
  GArray *values = NULL;
  DexTrampoline *state;
  DexPromise *promise;
  va_list args;

  values = g_array_new (FALSE, TRUE, sizeof (GValue));
  g_array_set_clear_func (values, (GDestroyNotify)g_value_unset);
  g_array_set_size (values, n_params);

  va_start (args, n_params);

  for (guint i = 0; i < n_params; i++)
    {
      GType gtype = va_arg (args, GType);
      GValue *dest = &g_array_index (values, GValue, i);
      g_auto(GValue) value = G_VALUE_INIT;

      G_VALUE_COLLECT_INIT (&value, gtype, args, 0, &errmsg);

      if (errmsg != NULL)
        break;

      g_value_init (dest, gtype);
      g_value_copy (&value, dest);
    }

  va_end (args);

  if (errmsg != NULL)
    {
      DexFuture *ret;

      ret = dex_future_new_reject (G_IO_ERROR,
                                   G_IO_ERROR_FAILED,
                                   "Failed to trampoline to fiber: %s",
                                   errmsg);
      g_free (errmsg);
      return ret;
    }

  promise = dex_promise_new ();

  state = g_new0 (DexTrampoline, 1);
  state->values = g_steal_pointer (&values);
  state->callback = callback;
  state->promise = dex_ref (promise);
  state->return_type = return_type;

  g_thread_unref (g_thread_new (name, dex_trampoline_thread, state));

  return DEX_FUTURE (promise);
}
