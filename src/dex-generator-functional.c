/* dex-generator-functional.c
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

#include "config.h"

#include <stdarg.h>

#include "dex-generator.h"
#include "dex-error.h"
#include "dex-future.h"
#include "dex-future-private.h"

typedef struct
{
  DexGenerator **generators;
  guint          n_generators;
} ZipState;

typedef struct
{
  DexGenerator   *source;
  GCallback       callback;
  gpointer        user_data;
  GDestroyNotify  destroy;
  gboolean        filter;
} TransformState;

static void
zip_state_free (gpointer data)
{
  ZipState *state = data;

  for (guint i = 0; i < state->n_generators; i++)
    {
      dex_generator_close (state->generators[i]);
      dex_unref (state->generators[i]);
    }

  g_free (state->generators);
  g_free (state);
}

static DexFuture *
zip_produce (gpointer data)
{
  ZipState *state = data;

  if (state->n_generators == 0)
    return dex_future_new_true ();

  for (;;)
    {
      GValue *values;
      DexTuple *tuple = NULL;
      GError *error = NULL;
      gboolean complete = TRUE;

      values = g_new0 (GValue, state->n_generators);

      for (guint i = 0; i < state->n_generators; i++)
        {
          if (!dex_generator_await_next (state->generators[i], &values[i], &error))
            {
              complete = FALSE;
              break;
            }
        }

      if (complete)
        tuple = dex_tuple_newv (values, state->n_generators);

      for (guint i = 0; i < state->n_generators; i++)
        if (G_IS_VALUE (&values[i]))
          g_value_unset (&values[i]);

      g_free (values);

      if (error != NULL)
        return dex_future_new_for_error (error);

      if (!complete)
        return dex_future_new_true ();

      complete = dex_fiber_yield_boxed (DEX_TYPE_TUPLE, tuple, &error);
      dex_tuple_unref (tuple);
      if (!complete)
        return dex_future_new_for_error (error);
    }
}

/**
 * dex_generator_zipv:
 * @generators: (array length=n_generators) (element-type Dex.Generator) (transfer none) (nullable): sources
 * @n_generators: number of sources
 *
 * Zips sources lazily, pulling left to right, one at a time. Like Python's
 * `zip()`, it stops at the shortest input. An earlier input can advance once
 * before a later input reports exhaustion. The sources are closed when the
 * result ends, fails, or closes. The result takes a reference to each source.
 * Zero inputs produce an empty generator.
 *
 * Returns: (transfer full): a generator of [struct@Dex.Tuple] values
 * Since: 1.3
 */
DexGenerator *
dex_generator_zipv (DexGenerator * const *generators,
                    guint                 n_generators)
{
  ZipState *state;

  g_return_val_if_fail (n_generators == 0 || generators != NULL, NULL);

  for (guint i = 0; i < n_generators; i++)
    g_return_val_if_fail (DEX_IS_GENERATOR (generators[i]), NULL);

  state = g_new0 (ZipState, 1);
  state->generators = g_new (DexGenerator *, n_generators);
  for (guint i = 0; i < n_generators; i++)
    state->generators[i] = dex_ref (generators[i]);
  state->n_generators = n_generators;

  return dex_generator_new_fiber (NULL, 0, zip_produce, state, zip_state_free);
}

/**
 * dex_generator_zip: (skip)
 * @first: (nullable) (transfer full): first source, or %NULL for no sources
 * @...: additional sources, terminated by %NULL
 *
 * Convenience form of dex_generator_zipv() for C callers.
 *
 * Returns: (transfer full): a generator of tuples
 * Since: 1.3
 */
DexGenerator *
dex_generator_zip (DexGenerator *first,
                   ...)
{
  GPtrArray *sources = g_ptr_array_new_with_free_func (dex_unref);
  DexGenerator *source = first;
  DexGenerator *result;
  va_list args;

  va_start (args, first);
  while (source != NULL)
    {
      g_ptr_array_add (sources, source);
      source = va_arg (args, DexGenerator *);
    }
  va_end (args);

  result = dex_generator_zipv ((DexGenerator **)sources->pdata, sources->len);
  g_ptr_array_unref (sources);
  return result;
}

static void
transform_state_free (gpointer data)
{
  TransformState *state = data;

  dex_generator_close (state->source);
  dex_unref (state->source);

  if (state->destroy != NULL)
    state->destroy (state->user_data);

  g_free (state);
}

static DexFuture *
transform_produce (gpointer data)
{
  TransformState *state = data;

  for (;;)
    {
      GValue output = G_VALUE_INIT;
      DexFuture *item;
      const GValue *input;
      GError *error = NULL;
      gboolean keep;

      if (!(item = dex_generator_next (state->source)))
        return dex_future_new_true ();

      input = dex_await_borrowed (item, &error);

      if (input == NULL || dex_generator_is_exhausted (state->source))
        {
          dex_unref (item);
          return error ? dex_future_new_for_error (error) : dex_future_new_true ();
        }

      if (state->filter)
        {
          keep = ((DexGeneratorFilterFunc)state->callback) (input, state->user_data);
        }
      else
        {
          ((DexGeneratorMapFunc)state->callback) (input, &output, state->user_data);

          keep = G_IS_VALUE (&output);

          if (!keep)
            g_set_error_literal (&error,
                                 DEX_ERROR,
                                 DEX_ERROR_NO_RESULT,
                                 "Map callback did not initialize its result");
        }

      if (error == NULL && keep)
        {
          const GValue *value = state->filter ? input : &output;

          if (!dex_fiber_yield_value (value, &error))
            g_assert (error != NULL);
        }

      if (G_IS_VALUE (&output))
        g_value_unset (&output);
      dex_unref (item);

      if (error != NULL)
        return dex_future_new_for_error (error);
    }
}

/**
 * dex_generator_map:
 * @source: (transfer full): source generator
 * @callback: (scope notified) (closure user_data) (destroy destroy): mapper
 * @user_data: (nullable): callback data
 * @destroy: (nullable): callback data destroy notify
 *
 * Maps each input lazily. The callback initializes an output #GValue, which
 * may have any type. Leaving it uninitialized fails with %DEX_ERROR_NO_RESULT.
 * Source rejection terminates the adapter.
 *
 * Returns: (transfer full): the mapped generator
 * Since: 1.3
 */
DexGenerator *
dex_generator_map (DexGenerator        *source,
                   DexGeneratorMapFunc  callback,
                   gpointer             user_data,
                   GDestroyNotify       destroy)
{
  TransformState *state;

  g_return_val_if_fail (DEX_IS_GENERATOR (source), NULL);
  g_return_val_if_fail (callback != NULL, NULL);

  state = g_new0 (TransformState, 1);
  state->source = source;
  state->callback = G_CALLBACK (callback);
  state->user_data = user_data;
  state->destroy = destroy;

  return dex_generator_new_fiber (NULL, 0, transform_produce, state, transform_state_free);
}

/**
 * dex_generator_filter:
 * @source: (transfer full): source generator
 * @callback: (scope notified) (closure user_data) (destroy destroy): predicate
 * @user_data: (nullable): callback data
 * @destroy: (nullable): callback data destroy notify
 *
 * Selects input values lazily using a synchronous predicate. Source rejection
 * terminates the adapter.
 *
 * Returns: (transfer full): the filtered generator
 * Since: 1.3
 */
DexGenerator *
dex_generator_filter (DexGenerator           *source,
                      DexGeneratorFilterFunc  callback,
                      gpointer                user_data,
                      GDestroyNotify          destroy)
{
  TransformState *state;

  g_return_val_if_fail (DEX_IS_GENERATOR (source), NULL);
  g_return_val_if_fail (callback != NULL, NULL);

  state = g_new0 (TransformState, 1);
  state->source = source;
  state->callback = G_CALLBACK (callback);
  state->user_data = user_data;
  state->destroy = destroy;
  state->filter = TRUE;

  return dex_generator_new_fiber (NULL, 0, transform_produce, state, transform_state_free);
}
