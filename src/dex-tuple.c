/* dex-tuple.c
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

#include "dex-tuple.h"

struct _DexTuple
{
  gatomicrefcount refs;
  guint           n_values;
  GValue          values[];
};

/**
 * DexTuple:
 *
 * An immutable row of owned values.
 *
 * Since: 1.3
 */
G_DEFINE_BOXED_TYPE (DexTuple, dex_tuple, dex_tuple_ref, dex_tuple_unref)

/**
 * dex_tuple_newv:
 * @values: (array length=n_values) (element-type GObject.Value) (nullable): values to copy
 * @n_values: number of values
 *
 * Copies each value into a new tuple.
 *
 * Returns: (transfer full): a new tuple
 * Since: 1.3
 */
DexTuple *
dex_tuple_newv (const GValue *values,
                guint         n_values)
{
  DexTuple *self;

  g_return_val_if_fail (n_values == 0 || values != NULL, NULL);

  for (guint i = 0; i < n_values; i++)
    g_return_val_if_fail (G_IS_VALUE (&values[i]), NULL);

  self = g_malloc0 (sizeof *self + sizeof (GValue) * n_values);
  g_atomic_ref_count_init (&self->refs);
  self->n_values = n_values;

  for (guint i = 0; i < n_values; i++)
    {
      g_value_init (&self->values[i], G_VALUE_TYPE (&values[i]));
      g_value_copy (&values[i], &self->values[i]);
    }

  return self;
}

/**
 * dex_tuple_ref:
 * @self: a tuple
 *
 * Returns: (transfer full): @self
 * Since: 1.3
 */
DexTuple *
dex_tuple_ref (DexTuple *self)
{
  g_return_val_if_fail (self != NULL, NULL);

  g_atomic_ref_count_inc (&self->refs);
  return self;
}

/**
 * dex_tuple_unref:
 * @self: a tuple
 *
 * Since: 1.3
 */
void
dex_tuple_unref (DexTuple *self)
{
  g_return_if_fail (self != NULL);

  if (g_atomic_ref_count_dec (&self->refs))
    {
      for (guint i = 0; i < self->n_values; i++)
        g_value_unset (&self->values[i]);
      g_free (self);
    }
}

/**
 * dex_tuple_get_size:
 * @self: a tuple
 *
 * Returns: the number of values
 * Since: 1.3
 */
guint
dex_tuple_get_size (DexTuple *self)
{
  g_return_val_if_fail (self != NULL, 0);

  return self->n_values;
}

/**
 * dex_tuple_get_value_at:
 * @self: a tuple
 * @position: zero-based position
 *
 * Returns: (transfer none) (nullable): the borrowed value, or %NULL if out of bounds
 * Since: 1.3
 */
const GValue *
dex_tuple_get_value_at (DexTuple *self,
                        guint     position)
{
  g_return_val_if_fail (self != NULL, NULL);

  return position < self->n_values ? &self->values[position] : NULL;
}
