/* test-future-list-model.c
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

#include <libdex.h>

#include <gio/gio.h>

static guint items_changed_count = 0;
static guint last_position = 0;
static guint last_removed = 0;
static guint last_added = 0;

static void
on_items_changed (GListModel *model,
                  guint       position,
                  guint       removed,
                  guint       added,
                  gpointer    user_data)
{
  items_changed_count++;
  last_position = position;
  last_removed = removed;
  last_added = added;
}

static void
test_future_list_model_basic (void)
{
  DexPromise *promise;
  GListModel *future_model;
  GListStore *base_store;
  GObject *item1, *item2, *item3;

  /* Reset counters */
  items_changed_count = 0;
  last_position = 0;
  last_removed = 0;
  last_added = 0;

  /* Create a promise that will resolve to a GListStore */
  promise = dex_promise_new ();
  future_model = dex_future_list_model_new (dex_ref (DEX_FUTURE (promise)));

  /* Before the future completes, it should have zero items */
  g_assert_cmpuint (g_list_model_get_n_items (future_model), ==, 0);

  /* Connect to items-changed signal */
  g_signal_connect (future_model, "items-changed", G_CALLBACK (on_items_changed), NULL);

  /* Create the base GListStore and add an item to it */
  base_store = g_list_store_new (G_TYPE_OBJECT);
  item1 = g_object_new (G_TYPE_OBJECT, NULL);
  g_list_store_append (base_store, item1);
  g_object_unref (item1);

  /* Resolve the promise with the base store */
  dex_promise_resolve_object (promise, g_object_ref (base_store));

  /* Process pending events to allow the future to resolve */
  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);

  /* Wait for the future to actually resolve */
  while (dex_future_get_status (DEX_FUTURE (promise)) == DEX_FUTURE_STATUS_PENDING)
    {
      if (!g_main_context_iteration (NULL, TRUE))
        break;
    }

  /* Check that items-changed was emitted when the model was added (since it had 1 item) */
  g_assert_cmpuint (items_changed_count, ==, 1);
  g_assert_cmpuint (last_added, ==, 1);
  g_assert_cmpuint (last_removed, ==, 0);
  g_assert_cmpuint (last_position, ==, 0);

  /* Add more items to the base model */
  item2 = g_object_new (G_TYPE_OBJECT, NULL);
  g_list_store_append (base_store, item2);
  g_object_unref (item2);

  /* Process pending events */
  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);

  /* Check that items-changed was emitted */
  g_assert_cmpuint (items_changed_count, ==, 2);
  g_assert_cmpuint (last_added, ==, 1);
  g_assert_cmpuint (last_removed, ==, 0);
  g_assert_cmpuint (last_position, ==, 1);

  /* Add another item */
  item3 = g_object_new (G_TYPE_OBJECT, NULL);
  g_list_store_append (base_store, item3);
  g_object_unref (item3);

  /* Process pending events */
  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);

  /* Check that items-changed was emitted for the addition */
  g_assert_cmpuint (items_changed_count, ==, 3);
  g_assert_cmpuint (last_added, ==, 1);
  g_assert_cmpuint (last_removed, ==, 0);
  g_assert_cmpuint (last_position, ==, 2);

  /* Verify the items are accessible */
  g_assert_cmpuint (g_list_model_get_n_items (future_model), ==, 3);
  {
    GObject *retrieved_item;

    retrieved_item = g_list_model_get_item (future_model, 0);
    g_assert_nonnull (retrieved_item);
    g_object_unref (retrieved_item);

    retrieved_item = g_list_model_get_item (future_model, 1);
    g_assert_nonnull (retrieved_item);
    g_object_unref (retrieved_item);

    retrieved_item = g_list_model_get_item (future_model, 2);
    g_assert_nonnull (retrieved_item);
    g_object_unref (retrieved_item);
  }

  g_clear_object (&future_model);
  g_clear_object (&base_store);
  dex_clear (&promise);
}

int
main (int   argc,
      char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Dex/TestSuite/FutureListModel/basic", test_future_list_model_basic);
  return g_test_run ();
}
