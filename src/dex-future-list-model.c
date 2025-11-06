/* dex-future-list-model.c
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

#include "dex-future-list-model.h"

/**
 * DexFutureListModel:
 *
 * This class provides a [iface@Gio.ListModel] implementation that will
 * expand to the contents of another [iface@Gio.ListModel] from a
 * [class@Dex.Future].
 *
 * Since: 1.1
 */

struct _DexFutureListModel
{
  GObject     parent_instance;
  GListModel *model;
  DexFuture  *future;
};

static GType
dex_future_list_model_get_item_type (GListModel *model)
{
  DexFutureListModel *self = DEX_FUTURE_LIST_MODEL (model);

  if (self->model != NULL)
    return g_list_model_get_item_type (self->model);

  return G_TYPE_OBJECT;
}

static guint
dex_future_list_model_get_n_items (GListModel *model)
{
  DexFutureListModel *self = DEX_FUTURE_LIST_MODEL (model);

  if (self->model != NULL)
    return g_list_model_get_n_items (self->model);

  return 0;
}

static gpointer
dex_future_list_model_get_item (GListModel *model,
                                guint       position)
{
  DexFutureListModel *self = DEX_FUTURE_LIST_MODEL (model);

  if (self->model != NULL)
    return g_list_model_get_item (self->model, position);

  return NULL;
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = dex_future_list_model_get_item_type;
  iface->get_n_items = dex_future_list_model_get_n_items;
  iface->get_item = dex_future_list_model_get_item;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (DexFutureListModel, dex_future_list_model, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static void
dex_future_list_model_finalize (GObject *object)
{
  DexFutureListModel *self = (DexFutureListModel *)object;

  dex_clear (&self->future);
  g_clear_object (&self->model);

  G_OBJECT_CLASS (dex_future_list_model_parent_class)->finalize (object);
}

static void
dex_future_list_model_class_init (DexFutureListModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = dex_future_list_model_finalize;
}

static void
dex_future_list_model_init (DexFutureListModel *self)
{
}

static DexFuture *
apply_model (DexFuture *future,
             gpointer   user_data)
{
  GWeakRef *weak_ref = user_data;
  DexFutureListModel *self = g_weak_ref_get (weak_ref);
  const GValue *value;
  GListModel *model;

  if (self != NULL)
    {
      if ((value = dex_future_get_value (future, NULL)) &&
          G_VALUE_HOLDS (value, G_TYPE_LIST_MODEL) &&
          (model = g_value_get_object (value)))
        {
          guint n_items = g_list_model_get_n_items (model);

          g_set_object (&self->model, model);

          g_signal_connect_object (model,
                                   "items-changed",
                                   G_CALLBACK (g_list_model_items_changed),
                                   self,
                                   G_CONNECT_SWAPPED);

          if (n_items > 0)
            g_list_model_items_changed (G_LIST_MODEL (self), 0, 0, n_items);
        }

      g_object_unref (self);
    }

  return dex_future_new_true ();
}

static GWeakRef *
_g_weak_ref_new (gpointer instance)
{
  GWeakRef *wr = g_new0 (GWeakRef, 1);
  g_weak_ref_init (wr, instance);
  return wr;
}

static void
_g_weak_ref_free (GWeakRef *wr)
{
  g_weak_ref_clear (wr);
  g_free (wr);
}

/**
 * dex_future_list_model_new:
 * @future: (transfer full): a [class@Dex.Future] that resolves to
 *   a [iface@Gio.ListModel].
 *
 * Creates a new list model that will initially be empty and after
 * @future resolves contain the items within it.
 *
 * Returns: (transfer full): a new [class@Dex.FutureListModel]
 *
 * Since: 1.1
 */
GListModel *
dex_future_list_model_new (DexFuture *future)
{
  DexFutureListModel *self;

  g_return_val_if_fail (DEX_IS_FUTURE (future), NULL);

  self = g_object_new (DEX_TYPE_FUTURE_LIST_MODEL, NULL);
  self->future = g_steal_pointer (&future);

  dex_future_disown (dex_future_then (dex_ref (self->future),
                                      apply_model,
                                      _g_weak_ref_new (self),
                                      (GDestroyNotify) _g_weak_ref_free));

  return G_LIST_MODEL (self);
}

/**
 * dex_future_list_model_dup_future:
 * @self: a [class@Dex.FutureListModel]
 *
 * Gets the future provided when creating the list model.
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves to a
 *   [iface@Gio.ListModel] or rejects with error.
 *
 * Since: 1.1
 */
DexFuture *
dex_future_list_model_dup_future (DexFutureListModel *self)
{
  dex_return_error_if_fail (DEX_IS_FUTURE_LIST_MODEL (self));

  return dex_ref (self->future);
}
