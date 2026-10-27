/* dex-graph.c
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

#include <gio/gio.h>

#include "dex-deferred.h"
#include "dex-error.h"
#include "dex-future-set.h"
#include "dex-graph.h"

struct _DexGraphNode
{
  DexGraphBuilder *builder;
  DexFuture       *future;
  GArray          *dependencies;
  GArray          *dependents;
  GHashTable      *dependency_set;
  guint            index;
};

struct _DexGraphBuilder
{
  gatomicrefcount  ref_count;
  DexScheduler    *scheduler;
  GPtrArray       *nodes;
  gboolean         built;
};

typedef struct
{
  DexScheduler *scheduler;
  GPtrArray    *nodes;
  GArray       *order;
} DexGraphPlan;

/**
 * DexGraphBuilder:
 *
 * Collects futures and the dependencies between them before building a graph.
 *
 * Since: 1.3
 */

G_DEFINE_BOXED_TYPE (DexGraphBuilder, dex_graph_builder, dex_graph_builder_ref, dex_graph_builder_free)

static void
dex_graph_node_free (gpointer data)
{
  DexGraphNode *node = data;

  dex_clear (&node->future);
  g_array_unref (node->dependencies);
  g_array_unref (node->dependents);
  g_clear_pointer (&node->dependency_set, g_hash_table_unref);
  g_free (node);
}

static void
dex_graph_plan_free (gpointer data)
{
  DexGraphPlan *plan = data;

  dex_clear (&plan->scheduler);
  g_ptr_array_unref (plan->nodes);
  g_array_unref (plan->order);
  g_free (plan);
}

/**
 * dex_graph_builder_new:
 * @scheduler: (nullable): the scheduler for graph coordination, or %NULL for
 *   the thread default
 *
 * Creates a builder for operations and their dependencies. The scheduler
 * coordinates readiness; each operation keeps its own execution target.
 *
 * Returns: (transfer full): a graph builder
 *
 * Since: 1.3
 */
DexGraphBuilder *
dex_graph_builder_new (DexScheduler *scheduler)
{
  DexGraphBuilder *builder;

  g_return_val_if_fail (scheduler == NULL || DEX_IS_SCHEDULER (scheduler), NULL);

  builder = g_new0 (DexGraphBuilder, 1);
  g_atomic_ref_count_init (&builder->ref_count);
  builder->scheduler = scheduler ? dex_ref (scheduler) : dex_scheduler_ref_thread_default ();
  if (builder->scheduler == NULL)
    builder->scheduler = dex_ref (dex_scheduler_get_default ());
  builder->nodes = g_ptr_array_new_with_free_func (dex_graph_node_free);

  return builder;
}

/**
 * dex_graph_builder_ref:
 * @builder: a graph builder
 *
 * Adds a reference to @builder.
 *
 * Returns: (transfer full): @builder
 *
 * Since: 1.3
 */
DexGraphBuilder *
dex_graph_builder_ref (DexGraphBuilder *builder)
{
  g_return_val_if_fail (builder != NULL, NULL);

  g_atomic_ref_count_inc (&builder->ref_count);
  return builder;
}

/**
 * dex_graph_builder_free:
 * @builder: (nullable): a graph builder
 *
 * Frees @builder. A successfully built graph keeps its nodes independently.
 *
 * Since: 1.3
 */
void
dex_graph_builder_free (DexGraphBuilder *builder)
{
  if (builder == NULL)
    return;

  if (!g_atomic_ref_count_dec (&builder->ref_count))
    return;

  dex_clear (&builder->scheduler);
  g_ptr_array_unref (builder->nodes);
  g_free (builder);
}

/**
 * dex_graph_builder_add:
 * @builder: a graph builder
 * @future: (transfer full): the operation to observe when the node is ready
 *
 * Adds an operation without observing it. Name @future before adding it if
 * a diagnostic label is useful. Node indices identify operations even when
 * names are absent or repeated.
 *
 * Returns: (transfer none) (nullable): the node
 *
 * Since: 1.3
 */
DexGraphNode *
dex_graph_builder_add (DexGraphBuilder *builder,
                       DexFuture       *future)
{
  DexGraphNode *node;

  g_return_val_if_fail (builder != NULL, NULL);
  g_return_val_if_fail (!builder->built, NULL);
  g_return_val_if_fail (DEX_IS_FUTURE (future), NULL);

  node = g_new0 (DexGraphNode, 1);
  node->builder = builder;
  node->future = future;
  node->index = builder->nodes->len;
  node->dependencies = g_array_new (FALSE, FALSE, sizeof (guint));
  node->dependents = g_array_new (FALSE, FALSE, sizeof (guint));

  g_ptr_array_add (builder->nodes, node);

  return node;
}

/**
 * dex_graph_builder_add_dependency:
 * @builder: a graph builder
 * @node: the operation that must wait
 * @dependency: the operation that must finish first
 *
 * Makes @node wait for @dependency to resolve. Duplicate relationships are
 * ignored. Both handles must belong to @builder.
 *
 * Returns: %TRUE if the relationship was accepted
 *
 * Since: 1.3
 */
gboolean
dex_graph_builder_add_dependency (DexGraphBuilder *builder,
                                  DexGraphNode    *node,
                                  DexGraphNode    *dependency)
{
  g_return_val_if_fail (builder != NULL, FALSE);
  g_return_val_if_fail (!builder->built, FALSE);
  g_return_val_if_fail (node != NULL && node->builder == builder, FALSE);
  g_return_val_if_fail (dependency != NULL && dependency->builder == builder, FALSE);

  if (node->dependency_set == NULL)
    node->dependency_set = g_hash_table_new (g_direct_hash, g_direct_equal);

  if (!g_hash_table_add (node->dependency_set, dependency))
    return TRUE;

  g_array_append_val (node->dependencies, dependency->index);
  g_array_append_val (dependency->dependents, node->index);

  return TRUE;
}

static DexFuture *
dex_graph_start_node (DexFuture *completed,
                      gpointer   user_data)
{
  g_assert (DEX_IS_FUTURE (completed));
  g_assert (DEX_IS_FUTURE (user_data));

  /* Returning the rejection keeps propagation inside the block's dispatch
   * recursion limit. The operation must remain unobserved on this path.
   */
  if (dex_future_is_rejected (completed))
    return dex_ref (completed);

  return dex_ref (user_data);
}

static DexFuture *
dex_graph_complete (DexFuture *completed,
                    gpointer   user_data)
{
  DexFutureSet *stages = DEX_FUTURE_SET (completed);

  g_assert (DEX_IS_FUTURE_SET (stages));

  /* allv() waits for every stage, but its status alone does not preserve
   * earlier rejections. Inspect each result before completing the graph.
   */
  for (guint i = 0; i < dex_future_set_get_size (stages); i++)
    {
      DexFuture *stage = dex_future_set_get_future_at (stages, i);

      if (dex_future_is_rejected (stage))
        return dex_future_new_reject (DEX_ERROR,
                                      DEX_ERROR_DEPENDENCY_FAILED,
                                      "One or more graph operations failed");
    }

  return NULL;
}

static DexFuture *
dex_graph_run (gpointer user_data)
{
  DexGraphPlan *plan = user_data;
  DexFuture **stages;
  DexFuture **observations;
  DexFuture *result;
  guint n_nodes = plan->nodes->len;

  if (n_nodes == 0)
    return dex_future_new_for_boolean (TRUE);

  stages = g_new0 (DexFuture *, n_nodes);
  observations = g_new0 (DexFuture *, n_nodes);

  for (guint i = 0; i < plan->order->len; i++)
    {
      guint index = g_array_index (plan->order, guint, i);
      DexGraphNode *node = g_ptr_array_index (plan->nodes, index);

      if (node->dependencies->len == 0)
        {
          stages[index] = dex_ref (node->future);
        }
      else
        {
          DexFuture **dependencies = g_new (DexFuture *, node->dependencies->len);
          DexFuture *gate;

          for (guint j = 0; j < node->dependencies->len; j++)
            {
              guint dependency = g_array_index (node->dependencies, guint, j);
              dependencies[j] = stages[dependency];
            }

          gate = dex_future_all_racev (dependencies, node->dependencies->len);

          /* Both outcomes must enter the scheduler. A then() block would
           * propagate rejection synchronously through the whole graph,
           * potentially overflowing the completing operation's fiber stack.
           */
          stages[index] = dex_future_finally (gate,
                                              dex_graph_start_node,
                                              dex_ref (node->future),
                                              dex_unref);
          g_free (dependencies);
        }

      /* Keep a graph-owned observation before a later dependency gate can
       * reject synchronously and discard its observation of this stage.
       * Store observations in reverse topological order so teardown releases
       * dependents while their dependencies still have their own observation,
       * avoiding recursive destruction of the entire chain.
       */
      observations[n_nodes - i - 1] = dex_future_allv (&stages[index], 1);
    }

  result = dex_future_allv (observations, n_nodes);
  result = dex_future_finally (result, dex_graph_complete, NULL, NULL);

  for (guint i = 0; i < n_nodes; i++)
    {
      dex_unref (stages[i]);
      dex_unref (observations[i]);
    }

  g_free (stages);
  g_free (observations);

  return result;
}

/**
 * dex_graph_builder_build:
 * @builder: a graph builder
 * @error: return location for an error, or %NULL
 *
 * Validates the dependency graph and creates a future that starts its ready
 * operations when observed. Each operation runs once, after all of its
 * dependencies resolve. A rejected dependency prevents its dependents from
 * starting. The graph resolves after every operation resolves, and rejects
 * if any operation rejects.
 *
 * A successful build consumes the nodes from @builder. The builder may then
 * be freed. Node handles are only used while configuring the builder.
 *
 * Returns: (transfer full) (nullable): the graph future, or %NULL on a cycle
 *
 * Since: 1.3
 */
DexFuture *
dex_graph_builder_build (DexGraphBuilder  *builder,
                         GError          **error)
{
  DexGraphPlan *plan;
  GArray *order;
  GQueue ready = G_QUEUE_INIT;
  guint *pending;
  guint n_nodes;

  g_return_val_if_fail (builder != NULL, NULL);
  g_return_val_if_fail (!builder->built, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  n_nodes = builder->nodes->len;
  pending = g_new (guint, n_nodes);
  order = g_array_sized_new (FALSE, FALSE, sizeof (guint), n_nodes);

  for (guint i = 0; i < n_nodes; i++)
    {
      DexGraphNode *node = g_ptr_array_index (builder->nodes, i);

      pending[i] = node->dependencies->len;
      if (pending[i] == 0)
        g_queue_push_tail (&ready, GUINT_TO_POINTER (i + 1));
    }

  while (!g_queue_is_empty (&ready))
    {
      guint index = GPOINTER_TO_UINT (g_queue_pop_head (&ready)) - 1;
      DexGraphNode *node = g_ptr_array_index (builder->nodes, index);

      g_array_append_val (order, index);

      for (guint i = 0; i < node->dependents->len; i++)
        {
          guint dependent = g_array_index (node->dependents, guint, i);

          if (--pending[dependent] == 0)
            g_queue_push_tail (&ready, GUINT_TO_POINTER (dependent + 1));
        }
    }

  if (order->len != n_nodes)
    {
      for (guint i = 0; i < n_nodes; i++)
        {
          if (pending[i] != 0)
            {
              DexGraphNode *node = g_ptr_array_index (builder->nodes, i);
              const char *name = dex_future_get_name (node->future);

              g_set_error (error,
                           DEX_ERROR,
                           DEX_ERROR_INVALID_TRANSITION,
                           "Graph contains a cycle involving node %u%s%s%s",
                           i,
                           name ? " (" : "",
                           name ? name : "",
                           name ? ")" : "");
              break;
            }
        }

      g_array_unref (order);
      g_free (pending);

      return NULL;
    }

  g_free (pending);

  plan = g_new0 (DexGraphPlan, 1);
  plan->scheduler = dex_ref (builder->scheduler);
  plan->nodes = g_steal_pointer (&builder->nodes);
  plan->order = order;

  for (guint i = 0; i < n_nodes; i++)
    {
      DexGraphNode *node = g_ptr_array_index (plan->nodes, i);

      node->builder = NULL;
      g_clear_pointer (&node->dependency_set, g_hash_table_unref);
    }

  builder->nodes = g_ptr_array_new_with_free_func (dex_graph_node_free);
  builder->built = TRUE;

  return DEX_FUTURE (dex_deferred_new (plan->scheduler,
                                       dex_graph_run,
                                       plan,
                                       dex_graph_plan_free));
}
