/* test-graph.c
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

#include <libdex.h>

typedef struct
{
  guint    *sequence;
  guint    *calls;
  guint     expected;
  gboolean  reject;
} Operation;

static DexFuture *
run_operation (gpointer data)
{
  Operation *operation = data;

  g_assert_cmpuint (*operation->sequence, ==, operation->expected);
  (*operation->sequence)++;
  (*operation->calls)++;

  if (operation->reject)
    return dex_future_new_reject (DEX_ERROR,
                                  DEX_ERROR_UNKNOWN,
                                  "operation failed");

  return dex_future_new_for_int (42);
}

static void
wait_for (DexFuture *future)
{
  gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

  while (dex_future_is_pending (future) && g_get_monotonic_time () < deadline)
    g_main_context_iteration (NULL, FALSE);

  g_assert_false (dex_future_is_pending (future));
}

static DexFuture *
pass_through (DexFuture *future,
              gpointer   data)
{
  return dex_ref (future);
}

static void
drain_context (void)
{
  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);
}

static void
assert_graph_result (DexFuture *graph,
                     gboolean   rejected)
{
  GError *error = NULL;
  const GValue *value;

  value = dex_future_get_value (graph, &error);
  if (rejected)
    {
      g_assert_true (dex_future_is_rejected (graph));
      g_assert_null (value);
      g_assert_error (error, DEX_ERROR, DEX_ERROR_DEPENDENCY_FAILED);
      g_clear_error (&error);
    }
  else
    {
      g_assert_true (dex_future_is_resolved (graph));
      g_assert_no_error (error);
      g_assert_nonnull (value);
      g_assert_true (g_value_get_boolean (value));
    }
}

static void
test_graph_order (void)
{
  DexGraphBuilder *builder = dex_graph_builder_new (NULL);
  DexGraphNode *a;
  DexGraphNode *b;
  DexGraphNode *c;
  DexFuture *graph;
  DexDeferred *future;
  GError *error = NULL;
  guint sequence = 0;
  guint calls = 0;
  Operation operations[] = {
    { &sequence, &calls, 0, FALSE },
    { &sequence, &calls, 1, FALSE },
    { &sequence, &calls, 2, FALSE },
  };

  future = dex_deferred_new (NULL, run_operation, &operations[0], NULL);
  dex_future_set_static_name (DEX_FUTURE (future), "same name");
  a = dex_graph_builder_add (builder, DEX_FUTURE (future));

  future = dex_deferred_new (NULL, run_operation, &operations[1], NULL);
  dex_future_set_static_name (DEX_FUTURE (future), "same name");
  b = dex_graph_builder_add (builder, DEX_FUTURE (future));

  future = dex_deferred_new (NULL, run_operation, &operations[2], NULL);
  c = dex_graph_builder_add (builder, DEX_FUTURE (future));

  g_assert_true (dex_graph_builder_add_dependency (builder, b, a));
  g_assert_true (dex_graph_builder_add_dependency (builder, c, b));
  g_assert_true (dex_graph_builder_add_dependency (builder, c, b));

  graph = dex_graph_builder_build (builder, &error);
  g_assert_no_error (error);
  g_assert_nonnull (graph);
  g_assert_cmpuint (calls, ==, 0);

  graph = dex_future_finally (graph, pass_through, NULL, NULL);
  wait_for (graph);
  g_assert_true (dex_future_is_resolved (graph));
  g_assert_cmpuint (calls, ==, 3);
  dex_unref (graph);
  dex_graph_builder_free (builder);
}

static void
test_graph_cycle (void)
{
  DexGraphBuilder *builder = dex_graph_builder_new (NULL);
  DexGraphNode *a;
  DexGraphNode *b;
  DexFuture *future;
  GError *error = NULL;

  future = dex_future_new_for_int (1);
  dex_future_set_static_name (future, "first");
  a = dex_graph_builder_add (builder, future);
  b = dex_graph_builder_add (builder, dex_future_new_for_int (2));
  g_assert_true (dex_graph_builder_add_dependency (builder, a, b));
  g_assert_true (dex_graph_builder_add_dependency (builder, b, a));

  g_assert_null (dex_graph_builder_build (builder, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_INVALID_TRANSITION);
  g_assert_nonnull (g_strstr_len (error->message, -1, "node 0 (first)"));
  g_clear_error (&error);
  dex_graph_builder_free (builder);
}

static void
test_graph_rejection (void)
{
  DexGraphBuilder *builder = dex_graph_builder_new (NULL);
  DexGraphNode *a;
  DexGraphNode *b;
  DexFuture *graph;
  GError *error = NULL;
  guint sequence = 0;
  guint calls = 0;
  Operation operations[] = {
    { &sequence, &calls, 0, TRUE },
    { &sequence, &calls, 1, FALSE },
  };

  a = dex_graph_builder_add (builder,
                             DEX_FUTURE (dex_deferred_new (NULL, run_operation,
                                                           &operations[0], NULL)));
  b = dex_graph_builder_add (builder,
                             DEX_FUTURE (dex_deferred_new (NULL, run_operation,
                                                           &operations[1], NULL)));
  g_assert_true (dex_graph_builder_add_dependency (builder, b, a));
  graph = dex_graph_builder_build (builder, &error);
  g_assert_no_error (error);
  g_assert_nonnull (graph);

  graph = dex_future_finally (graph, pass_through, NULL, NULL);
  wait_for (graph);
  g_assert_true (dex_future_is_rejected (graph));
  g_assert_null (dex_future_get_value (graph, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_DEPENDENCY_FAILED);
  g_assert_cmpuint (calls, ==, 1);
  g_clear_error (&error);
  dex_unref (graph);
  dex_graph_builder_free (builder);
}

static void
test_graph_completed_rejection (void)
{
  for (guint rejected_first = 0; rejected_first < 2; rejected_first++)
    {
      DexGraphBuilder *builder = dex_graph_builder_new (NULL);
      DexFuture *failed;
      DexFuture *succeeded;
      DexFuture *graph;
      GError *error = NULL;

      failed = dex_future_new_reject (DEX_ERROR,
                                      DEX_ERROR_UNKNOWN,
                                      "operation failed");
      succeeded = dex_future_new_for_int (42);

      dex_graph_builder_add (builder, rejected_first ? failed : succeeded);
      dex_graph_builder_add (builder, rejected_first ? succeeded : failed);
      graph = dex_graph_builder_build (builder, &error);
      g_assert_no_error (error);
      g_assert_nonnull (graph);
      dex_graph_builder_free (builder);

      g_assert_false (dex_await_boolean (graph, &error));
      g_assert_error (error, DEX_ERROR, DEX_ERROR_DEPENDENCY_FAILED);
      g_clear_error (&error);
    }
}

static void
test_graph_pending_rejection (void)
{
  for (guint rejected_first = 0; rejected_first < 2; rejected_first++)
    {
      DexGraphBuilder *builder = dex_graph_builder_new (NULL);
      DexPromise *failed = dex_promise_new ();
      DexPromise *succeeded = dex_promise_new ();
      DexFuture *graph;
      GError *error = NULL;

      dex_graph_builder_add (builder, dex_ref (DEX_FUTURE (failed)));
      dex_graph_builder_add (builder, dex_ref (DEX_FUTURE (succeeded)));
      graph = dex_graph_builder_build (builder, &error);
      g_assert_no_error (error);
      g_assert_nonnull (graph);
      dex_graph_builder_free (builder);

      graph = dex_future_finally (graph, pass_through, NULL, NULL);
      while (g_main_context_pending (NULL))
        g_main_context_iteration (NULL, FALSE);
      g_assert_true (dex_future_is_pending (graph));

      if (rejected_first)
        dex_promise_reject (failed,
                            g_error_new_literal (DEX_ERROR,
                                                 DEX_ERROR_UNKNOWN,
                                                 "operation failed"));
      else
        dex_promise_resolve_int (succeeded, 42);

      while (g_main_context_pending (NULL))
        g_main_context_iteration (NULL, FALSE);
      g_assert_true (dex_future_is_pending (graph));

      if (rejected_first)
        dex_promise_resolve_int (succeeded, 42);
      else
        dex_promise_reject (failed,
                            g_error_new_literal (DEX_ERROR,
                                                 DEX_ERROR_UNKNOWN,
                                                 "operation failed"));

      wait_for (graph);
      g_assert_true (dex_future_is_rejected (graph));
      g_assert_null (dex_future_get_value (graph, &error));
      g_assert_error (error, DEX_ERROR, DEX_ERROR_DEPENDENCY_FAILED);
      g_clear_error (&error);
      dex_unref (graph);
      dex_unref (failed);
      dex_unref (succeeded);
    }
}

static void
test_graph_shared_dependency (void)
{
  for (guint rejected_first = 0; rejected_first < 2; rejected_first++)
    {
      DexGraphBuilder *builder = dex_graph_builder_new (NULL);
      DexGraphNode *a;
      DexGraphNode *b;
      DexGraphNode *c;
      DexGraphNode *d;
      DexFuture *graph;
      GError *error = NULL;
      guint sequence = 0;
      guint calls_a = 0;
      guint calls_c = 0;
      guint calls_d = 0;
      Operation operations[] = {
        { &sequence, &calls_a, 0, FALSE },
        { &sequence, &calls_c, 1, FALSE },
        { &sequence, &calls_d, 1, FALSE },
      };

      b = dex_graph_builder_add (builder,
                                 dex_future_new_reject (DEX_ERROR,
                                                        DEX_ERROR_UNKNOWN,
                                                        "operation failed"));
      a = dex_graph_builder_add (builder,
                                 DEX_FUTURE (dex_deferred_new (NULL, run_operation,
                                                               &operations[0], NULL)));
      c = dex_graph_builder_add (builder,
                                 DEX_FUTURE (dex_deferred_new (NULL, run_operation,
                                                               &operations[1], NULL)));
      d = dex_graph_builder_add (builder,
                                 DEX_FUTURE (dex_deferred_new (NULL, run_operation,
                                                               &operations[2], NULL)));

      g_assert_true (dex_graph_builder_add_dependency (builder, c, rejected_first ? b : a));
      g_assert_true (dex_graph_builder_add_dependency (builder, c, rejected_first ? a : b));
      g_assert_true (dex_graph_builder_add_dependency (builder, d, a));

      graph = dex_graph_builder_build (builder, &error);
      g_assert_no_error (error);
      g_assert_nonnull (graph);
      dex_graph_builder_free (builder);
      g_assert_cmpuint (sequence, ==, 0);

      g_assert_false (dex_await_boolean (graph, &error));
      g_assert_error (error, DEX_ERROR, DEX_ERROR_DEPENDENCY_FAILED);
      g_assert_cmpuint (calls_a, ==, 1);
      g_assert_cmpuint (calls_c, ==, 0);
      g_assert_cmpuint (calls_d, ==, 1);
      g_clear_error (&error);
    }
}

static void
test_graph_empty (void)
{
  DexGraphBuilder *builder = dex_graph_builder_new (NULL);
  DexFuture *graph;
  GError *error = NULL;

  graph = dex_graph_builder_build (builder, &error);
  g_assert_no_error (error);
  g_assert_nonnull (graph);
  graph = dex_future_finally (graph, pass_through, NULL, NULL);
  wait_for (graph);
  g_assert_true (dex_future_is_resolved (graph));
  dex_unref (graph);
  dex_graph_builder_free (builder);
}

static void
test_graph_many_dependencies (void)
{
  DexGraphBuilder *builder = dex_graph_builder_new (NULL);
  DexGraphNode *target;
  DexFuture *graph;
  GError *error = NULL;
  guint sequence = 0;
  guint calls = 0;
  Operation operation = { &sequence, &calls, 0, FALSE };

  target = dex_graph_builder_add (builder,
                                  DEX_FUTURE (dex_deferred_new (NULL, run_operation,
                                                                &operation, NULL)));

  for (guint i = 0; i < 512; i++)
    {
      DexGraphNode *dependency;

      dependency = dex_graph_builder_add (builder, dex_future_new_for_int (i));
      g_assert_true (dex_graph_builder_add_dependency (builder, target, dependency));
      g_assert_true (dex_graph_builder_add_dependency (builder, target, dependency));
    }

  graph = dex_graph_builder_build (builder, &error);
  g_assert_no_error (error);
  g_assert_nonnull (graph);
  g_assert_cmpuint (calls, ==, 0);

  graph = dex_future_finally (graph, pass_through, NULL, NULL);
  wait_for (graph);
  g_assert_true (dex_future_is_resolved (graph));
  g_assert_cmpuint (calls, ==, 1);
  dex_unref (graph);
  dex_graph_builder_free (builder);
}

static void
test_graph_deep_chain (void)
{
  const guint n_nodes = 1024;
  const int failures[] = { 0, 512, 1023, -1 };

  for (guint fiber = 0; fiber < 2; fiber++)
    {
      for (guint failure = 0; failure < G_N_ELEMENTS (failures); failure++)
        {
          DexGraphBuilder *builder = dex_graph_builder_new (NULL);
          DexGraphNode *previous = NULL;
          Operation *operations = g_new0 (Operation, n_nodes);
          DexFuture *graph;
          GError *error = NULL;
          guint sequence = 0;
          guint calls = 0;

          for (guint i = 0; i < n_nodes; i++)
            {
              DexDeferred *future;
              DexGraphNode *node;

              operations[i] = (Operation) { &sequence, &calls, i, i == failures[failure] };
              if (fiber)
                future = dex_deferred_new_fiber (NULL, 0, run_operation, &operations[i], NULL);
              else
                future = dex_deferred_new (NULL, run_operation, &operations[i], NULL);
              node = dex_graph_builder_add (builder, DEX_FUTURE (future));
              if (previous != NULL)
                g_assert_true (dex_graph_builder_add_dependency (builder, node, previous));
              previous = node;
            }

          graph = dex_graph_builder_build (builder, &error);
          g_assert_no_error (error);
          g_assert_nonnull (graph);
          dex_graph_builder_free (builder);

          if (failures[failure] < 0)
            {
              g_assert_true (dex_await_boolean (graph, &error));
              g_assert_no_error (error);
              g_assert_cmpuint (calls, ==, n_nodes);
            }
          else
            {
              g_assert_false (dex_await_boolean (graph, &error));
              g_assert_error (error, DEX_ERROR, DEX_ERROR_DEPENDENCY_FAILED);
              g_assert_cmpuint (calls, ==, failures[failure] + 1);
              g_clear_error (&error);
            }

          g_free (operations);
        }
    }
}

typedef struct
{
  DexPromise *promise;
  guint       calls;
  guint       destroyed;
} PendingOperation;

static DexFuture *
run_pending_operation (gpointer data)
{
  PendingOperation *operation = data;

  g_assert_cmpuint (operation->destroyed, ==, 0);
  operation->calls++;
  return dex_ref (DEX_FUTURE (operation->promise));
}

static void
destroy_pending_operation (gpointer data)
{
  PendingOperation *operation = data;

  g_assert_cmpuint (operation->destroyed, ==, 0);
  operation->destroyed++;
}

static void
test_graph_dag_combination (guint        edges,
                            guint        failures,
                            const guint *order,
                            gboolean     reverse)
{
  DexGraphBuilder *builder = dex_graph_builder_new (NULL);
  DexGraphNode *nodes[4];
  PendingOperation operations[4] = { 0 };
  guint dependencies[4] = { 0 };
  DexFuture *graph;
  GError *error = NULL;
  guint settled = 0;
  guint edge = 0;

  for (guint i = 0; i < 4; i++)
    {
      guint index = reverse ? 3 - i : i;
      DexDeferred *future;

      operations[index].promise = dex_promise_new ();
      future = dex_deferred_new (NULL, run_pending_operation, &operations[index], NULL);
      nodes[index] = dex_graph_builder_add (builder, DEX_FUTURE (future));
    }

  for (guint i = 0; i < 4; i++)
    {
      for (guint j = 0; j < i; j++, edge++)
        {
          if ((edges & (1 << edge)) == 0)
            continue;

          dependencies[i] |= 1 << j;
          g_assert_true (dex_graph_builder_add_dependency (builder,
                                                           nodes[i], nodes[j]));
          g_assert_true (dex_graph_builder_add_dependency (builder,
                                                           nodes[i], nodes[j]));
        }
    }

  graph = dex_graph_builder_build (builder, &error);
  g_assert_no_error (error);
  g_assert_nonnull (graph);
  dex_graph_builder_free (builder);
  graph = dex_future_finally (graph, pass_through, NULL, NULL);

  for (guint step = 0; step <= 4; step++)
    {
      guint succeeded = 0;
      guint rejected = 0;

      if (step > 0)
        {
          guint index = order[step - 1];

          settled |= 1 << index;
          if (failures & (1 << index))
            dex_promise_reject (operations[index].promise,
                                g_error_new_literal (DEX_ERROR,
                                                     DEX_ERROR_UNKNOWN,
                                                     "operation failed"));
          else
            dex_promise_resolve_int (operations[index].promise, index);
        }

      drain_context ();

      /* Compute readiness independently of the graph using only
       * the edges and the results supplied by this test.
       */
      for (guint i = 0; i < 4; i++)
        {
          gboolean ready = (dependencies[i] & succeeded) == dependencies[i];

          g_assert_cmpuint (operations[i].calls, ==, ready ? 1 : 0);

          if (dependencies[i] & rejected)
            rejected |= 1 << i;
          else if (ready && (settled & (1 << i)))
            {
              if (failures & (1 << i))
                rejected |= 1 << i;
              else
                succeeded |= 1 << i;
            }
        }

      if ((succeeded | rejected) == 15)
        assert_graph_result (graph, rejected != 0);
      else
        g_assert_true (dex_future_is_pending (graph));
    }

  dex_unref (graph);
  for (guint i = 0; i < 4; i++)
    dex_unref (operations[i].promise);
}

static void
test_graph_dag_combinations (void)
{
  const guint completion_orders[][4] = {
    { 0, 1, 2, 3 },
    { 0, 1, 3, 2 },
    { 0, 2, 1, 3 },
    { 0, 2, 3, 1 },
    { 0, 3, 1, 2 },
    { 0, 3, 2, 1 },
    { 1, 0, 2, 3 },
    { 1, 0, 3, 2 },
    { 1, 2, 0, 3 },
    { 1, 2, 3, 0 },
    { 1, 3, 0, 2 },
    { 1, 3, 2, 0 },
    { 2, 0, 1, 3 },
    { 2, 0, 3, 1 },
    { 2, 1, 0, 3 },
    { 2, 1, 3, 0 },
    { 2, 3, 0, 1 },
    { 2, 3, 1, 0 },
    { 3, 0, 1, 2 },
    { 3, 0, 2, 1 },
    { 3, 1, 0, 2 },
    { 3, 1, 2, 0 },
    { 3, 2, 0, 1 },
    { 3, 2, 1, 0 },
  };

  /* Four topologically numbered nodes have six possible edges. Enumerate
   * every subset, every set of failing operations, all completion orders,
   * and both insertion orders, including completion before observation.
   */
  for (guint edges = 0; edges < 64; edges++)
    {
      for (guint failures = 0; failures < 16; failures++)
        {
          for (guint reverse = 0; reverse < 2; reverse++)
            {
              for (guint i = 0; i < G_N_ELEMENTS (completion_orders); i++)
                test_graph_dag_combination (edges, failures, completion_orders[i], reverse);
            }
        }
    }
}

static void
test_graph_pending_fan_in (void)
{
  const guint n_dependencies = 512;

  for (int failure = -1; failure < 2; failure++)
    {
      DexGraphBuilder *builder = dex_graph_builder_new (NULL);
      DexPromise **promises = g_new (DexPromise *, n_dependencies);
      DexGraphNode *target;
      DexDeferred *future;
      DexFuture *graph;
      GError *error = NULL;
      guint calls = 0;
      guint sequence = 0;
      Operation operation = { &sequence, &calls, 0, FALSE };

      future = dex_deferred_new (NULL, run_operation, &operation, NULL);
      target = dex_graph_builder_add (builder, DEX_FUTURE (future));

      for (guint i = 0; i < n_dependencies; i++)
        {
          DexGraphNode *dependency;

          promises[i] = dex_promise_new ();
          dependency = dex_graph_builder_add (builder, dex_ref (DEX_FUTURE (promises[i])));
          g_assert_true (dex_graph_builder_add_dependency (builder, target, dependency));
        }

      graph = dex_graph_builder_build (builder, &error);
      g_assert_no_error (error);
      g_assert_nonnull (graph);
      dex_graph_builder_free (builder);
      graph = dex_future_finally (graph, pass_through, NULL, NULL);
      drain_context ();

      for (guint i = 0; i < n_dependencies; i++)
        {
          guint index = n_dependencies - i - 1;

          g_assert_cmpuint (calls, ==, 0);
          g_assert_true (dex_future_is_pending (graph));

          if ((failure == 0 && i == 0) || (failure == 1 && index == 0))
            dex_promise_reject (promises[index],
                                g_error_new_literal (DEX_ERROR,
                                                     DEX_ERROR_UNKNOWN,
                                                     "operation failed"));
          else
            dex_promise_resolve_int (promises[index], index);

          drain_context ();
        }

      assert_graph_result (graph, failure >= 0);
      g_assert_cmpuint (calls, ==, failure >= 0 ? 0 : 1);
      dex_unref (graph);
      for (guint i = 0; i < n_dependencies; i++)
        dex_unref (promises[i]);
      g_free (promises);
    }
}

static void
test_graph_wide_rejection (void)
{
  DexGraphBuilder *builder = dex_graph_builder_new (NULL);
  DexGraphNode *root;
  DexGraphNode *leaf;
  DexDeferred *future;
  DexFuture *graph;
  GError *error = NULL;
  guint sequence = 0;
  guint root_calls = 0;
  guint skipped_calls = 0;
  Operation root_operation = { &sequence, &root_calls, 0, TRUE };
  Operation skipped_operation = { &sequence, &skipped_calls, 1, FALSE };

  future = dex_deferred_new_fiber (NULL, 0, run_operation, &root_operation, NULL);
  root = dex_graph_builder_add (builder, DEX_FUTURE (future));
  future = dex_deferred_new (NULL, run_operation, &skipped_operation, NULL);
  leaf = dex_graph_builder_add (builder, DEX_FUTURE (future));

  for (guint i = 0; i < 512; i++)
    {
      DexGraphNode *branch;

      future = dex_deferred_new (NULL, run_operation, &skipped_operation, NULL);
      branch = dex_graph_builder_add (builder, DEX_FUTURE (future));
      g_assert_true (dex_graph_builder_add_dependency (builder, branch, root));
      g_assert_true (dex_graph_builder_add_dependency (builder, leaf, branch));
    }

  graph = dex_graph_builder_build (builder, &error);
  g_assert_no_error (error);
  g_assert_nonnull (graph);
  dex_graph_builder_free (builder);

  g_assert_false (dex_await_boolean (graph, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_DEPENDENCY_FAILED);
  g_assert_cmpuint (root_calls, ==, 1);
  g_assert_cmpuint (skipped_calls, ==, 0);
  g_clear_error (&error);
}

typedef struct
{
  guint    calls;
  guint    destroyed;
  gboolean reject;
} TrackedOperation;

static DexFuture *
run_tracked_operation (gpointer data)
{
  TrackedOperation *operation = data;

  g_assert_cmpuint (operation->destroyed, ==, 0);
  operation->calls++;
  if (operation->reject)
    return dex_future_new_reject (DEX_ERROR,
                                  DEX_ERROR_UNKNOWN,
                                  "operation failed");

  return dex_future_new_true ();
}

static void
destroy_tracked_operation (gpointer data)
{
  TrackedOperation *operation = data;

  g_assert_cmpuint (operation->destroyed, ==, 0);
  operation->destroyed++;
}

static void
test_graph_lifetime (void)
{
  for (guint mode = 0; mode < 4; mode++)
    {
      DexGraphBuilder *builder = dex_graph_builder_new (NULL);
      TrackedOperation operations[3] = { 0 };
      DexGraphNode *previous = NULL;
      DexFuture *graph;
      GError *error = NULL;

      operations[1].reject = mode == 3;
      for (guint i = 0; i < G_N_ELEMENTS (operations); i++)
        {
          DexGraphNode *node;
          DexDeferred *future;

          future = dex_deferred_new (NULL, run_tracked_operation,
                                     &operations[i], destroy_tracked_operation);
          node = dex_graph_builder_add (builder, DEX_FUTURE (future));
          if (previous != NULL)
            g_assert_true (dex_graph_builder_add_dependency (builder, node, previous));
          previous = node;
        }

      /* A boxed reference keeps an unbuilt builder and its operations alive. */
      g_assert_true (dex_graph_builder_ref (builder) == builder);
      dex_graph_builder_free (builder);
      drain_context ();
      for (guint i = 0; i < G_N_ELEMENTS (operations); i++)
        {
          g_assert_cmpuint (operations[i].calls, ==, 0);
          g_assert_cmpuint (operations[i].destroyed, ==, 0);
        }

      if (mode == 0)
        dex_graph_builder_free (builder);
      else
        {
          graph = dex_graph_builder_build (builder, &error);
          g_assert_no_error (error);
          g_assert_nonnull (graph);
          dex_graph_builder_free (builder);

          /* Building and querying status must not start deferred operations. */
          g_assert_true (dex_future_is_pending (graph));
          g_assert_null (dex_future_get_value (graph, &error));
          g_assert_error (error, DEX_ERROR, DEX_ERROR_PENDING);
          g_clear_error (&error);
          drain_context ();
          for (guint i = 0; i < G_N_ELEMENTS (operations); i++)
            {
              g_assert_cmpuint (operations[i].calls, ==, 0);
              g_assert_cmpuint (operations[i].destroyed, ==, 0);
            }

          if (mode >= 2)
            {
              DexFuture *first;
              DexFuture *second;

              first = dex_future_finally (dex_ref (graph), pass_through, NULL, NULL);
              second = dex_future_finally (dex_ref (graph), pass_through, NULL, NULL);
              dex_unref (first);
              wait_for (second);
              assert_graph_result (second, mode == 3);
              dex_unref (second);
            }

          dex_unref (graph);
        }

      drain_context ();
      for (guint i = 0; i < G_N_ELEMENTS (operations); i++)
        {
          g_assert_cmpuint (operations[i].calls, ==, mode < 2 || (mode == 3 && i == 2) ? 0 : 1);
          g_assert_cmpuint (operations[i].destroyed, ==, 1);
        }
    }

  dex_graph_builder_free (NULL);
}

static void
test_graph_cycle_variants (void)
{
  for (guint self_cycle = 0; self_cycle < 2; self_cycle++)
    {
      DexGraphBuilder *builder = dex_graph_builder_new (NULL);
      TrackedOperation operations[4] = { 0 };
      DexGraphNode *nodes[4];
      GError *error = NULL;

      for (guint i = 0; i < G_N_ELEMENTS (nodes); i++)
        {
          DexDeferred *future;

          future = dex_deferred_new (NULL, run_tracked_operation,
                                     &operations[i], destroy_tracked_operation);
          nodes[i] = dex_graph_builder_add (builder, DEX_FUTURE (future));
        }

      /* Node zero is independent: a valid component must not start if
       * another component contains a cycle, including a self dependency.
       */
      if (self_cycle)
        g_assert_true (dex_graph_builder_add_dependency (builder, nodes[1], nodes[1]));
      else
        {
          g_assert_true (dex_graph_builder_add_dependency (builder, nodes[1], nodes[2]));
          g_assert_true (dex_graph_builder_add_dependency (builder, nodes[2], nodes[3]));
          g_assert_true (dex_graph_builder_add_dependency (builder, nodes[3], nodes[1]));
        }

      for (guint attempt = 0; attempt < 2; attempt++)
        {
          g_assert_null (dex_graph_builder_build (builder, &error));
          g_assert_error (error, DEX_ERROR, DEX_ERROR_INVALID_TRANSITION);
          g_assert_nonnull (g_strstr_len (error->message, -1, "node 1"));
          g_clear_error (&error);
        }
      g_assert_null (dex_graph_builder_build (builder, NULL));
      drain_context ();
      for (guint i = 0; i < G_N_ELEMENTS (operations); i++)
        {
          g_assert_cmpuint (operations[i].calls, ==, 0);
          g_assert_cmpuint (operations[i].destroyed, ==, 0);
        }
      dex_graph_builder_free (builder);
      for (guint i = 0; i < G_N_ELEMENTS (operations); i++)
        g_assert_cmpuint (operations[i].destroyed, ==, 1);
    }
}

typedef struct
{
  GThread  *main_thread;
  gboolean  on_main_thread;
  gboolean  reject;
  int       calls;
} TargetOperation;

static DexFuture *
run_target_operation (gpointer data)
{
  TargetOperation *operation = data;

  g_assert_true ((g_thread_self () == operation->main_thread) == operation->on_main_thread);
  g_atomic_int_inc (&operation->calls);
  if (operation->reject)
    return dex_future_new_reject (DEX_ERROR,
                                  DEX_ERROR_UNKNOWN,
                                  "operation failed");

  return dex_future_new_true ();
}

static void
test_graph_schedulers (void)
{
  DexScheduler *main_scheduler = dex_scheduler_get_default ();
  DexScheduler *worker_scheduler = dex_thread_pool_scheduler_get_default ();

  for (guint coordinator = 0; coordinator < 2; coordinator++)
    {
      for (guint worker = 0; worker < 2; worker++)
        {
          for (int failure = -1; failure < 2; failure++)
            {
              DexGraphBuilder *builder;
              DexGraphNode *nodes[2];
              TargetOperation operations[2] = { 0 };
              DexFuture *graph;
              GError *error = NULL;

              builder = dex_graph_builder_new (coordinator ? worker_scheduler : main_scheduler);
              for (guint i = 0; i < 2; i++)
                {
                  DexScheduler *scheduler;
                  DexDeferred *future;

                  operations[i].main_thread = g_thread_self ();
                  operations[i].on_main_thread = i == worker;
                  operations[i].reject = i == failure;
                  scheduler = operations[i].on_main_thread ? main_scheduler : worker_scheduler;
                  future = dex_deferred_new_fiber (scheduler, 0, run_target_operation,
                                                   &operations[i], NULL);
                  nodes[i] = dex_graph_builder_add (builder, DEX_FUTURE (future));
                }
              g_assert_true (dex_graph_builder_add_dependency (builder, nodes[1], nodes[0]));
              graph = dex_graph_builder_build (builder, &error);
              g_assert_no_error (error);
              g_assert_nonnull (graph);
              dex_graph_builder_free (builder);

              if (failure < 0)
                {
                  g_assert_true (dex_await_boolean (graph, &error));
                  g_assert_no_error (error);
                }
              else
                {
                  g_assert_false (dex_await_boolean (graph, &error));
                  g_assert_error (error, DEX_ERROR, DEX_ERROR_DEPENDENCY_FAILED);
                  g_clear_error (&error);
                }

              g_assert_cmpint (g_atomic_int_get (&operations[0].calls), ==, 1);
              g_assert_cmpint (g_atomic_int_get (&operations[1].calls), ==, failure == 0 ? 0 : 1);
            }
        }
    }
}

static void
test_graph_shared_operation (void)
{
  for (guint reject = 0; reject < 2; reject++)
    {
      DexGraphBuilder *builder = dex_graph_builder_new (NULL);
      TrackedOperation operations[2] = { 0 };
      DexDeferred *shared;
      DexDeferred *future;
      DexGraphNode *a;
      DexGraphNode *b;
      DexGraphNode *c;
      DexFuture *graph;
      GError *error = NULL;

      operations[0].reject = reject;
      shared = dex_deferred_new (NULL, run_tracked_operation,
                                 &operations[0], destroy_tracked_operation);
      a = dex_graph_builder_add (builder, dex_ref (DEX_FUTURE (shared)));
      b = dex_graph_builder_add (builder, DEX_FUTURE (shared));
      future = dex_deferred_new (NULL, run_tracked_operation,
                                 &operations[1], destroy_tracked_operation);
      c = dex_graph_builder_add (builder, DEX_FUTURE (future));
      g_assert_true (dex_graph_builder_add_dependency (builder, c, a));
      g_assert_true (dex_graph_builder_add_dependency (builder, c, b));

      graph = dex_graph_builder_build (builder, &error);
      g_assert_no_error (error);
      g_assert_nonnull (graph);
      dex_graph_builder_free (builder);
      graph = dex_future_finally (graph, pass_through, NULL, NULL);
      wait_for (graph);
      assert_graph_result (graph, reject);
      dex_unref (graph);
      drain_context ();

      g_assert_cmpuint (operations[0].calls, ==, 1);
      g_assert_cmpuint (operations[1].calls, ==, reject ? 0 : 1);
      g_assert_cmpuint (operations[0].destroyed, ==, 1);
      g_assert_cmpuint (operations[1].destroyed, ==, 1);
    }
}

static void
test_graph_discard (void)
{
  for (guint started = 0; started < 2; started++)
    {
      DexGraphBuilder *builder = dex_graph_builder_new (NULL);
      PendingOperation root = { dex_promise_new_cancellable (), 0 };
      TrackedOperation dependent = { 0 };
      DexGraphNode *a;
      DexGraphNode *b;
      DexDeferred *future;
      DexFuture *graph;
      GError *error = NULL;
      GCancellable *cancellable;

      future = dex_deferred_new (NULL, run_pending_operation, &root, NULL);
      a = dex_graph_builder_add (builder, DEX_FUTURE (future));
      future = dex_deferred_new (NULL,
                                 run_tracked_operation,
                                 &dependent,
                                 destroy_tracked_operation);
      b = dex_graph_builder_add (builder, DEX_FUTURE (future));
      g_assert_true (dex_graph_builder_add_dependency (builder, b, a));

      graph = dex_graph_builder_build (builder, &error);
      g_assert_no_error (error);
      g_assert_nonnull (graph);
      dex_graph_builder_free (builder);
      graph = dex_future_finally (graph, pass_through, NULL, NULL);
      if (started)
        drain_context ();
      g_assert_true (dex_future_is_pending (graph));
      dex_unref (graph);
      drain_context ();

      g_assert_cmpuint (root.calls, ==, started);
      g_assert_cmpuint (dependent.calls, ==, 0);
      g_assert_cmpuint (dependent.destroyed, ==, 1);
      cancellable = dex_promise_get_cancellable (root.promise);
      g_assert_true (g_cancellable_is_cancelled (cancellable) == started);

      /* A late result after discard must not start any dependent work. */
      dex_promise_resolve_int (root.promise, 42);
      drain_context ();
      g_assert_cmpuint (dependent.calls, ==, 0);
      dex_unref (root.promise);
    }
}

static void
test_graph_nested (void)
{
  for (guint reject = 0; reject < 2; reject++)
    {
      DexGraphBuilder *inner = dex_graph_builder_new (NULL);
      DexGraphBuilder *outer = dex_graph_builder_new (NULL);
      DexGraphNode *a;
      DexGraphNode *b;
      DexDeferred *future;
      DexFuture *graph;
      GError *error = NULL;
      guint sequence = 0;
      guint calls = 0;
      Operation operations[] = {
        { &sequence, &calls, 0, FALSE },
        { &sequence, &calls, 1, reject },
        { &sequence, &calls, 2, FALSE },
      };

      future = dex_deferred_new_fiber (NULL, 0, run_operation, &operations[1], NULL);
      dex_graph_builder_add (inner, DEX_FUTURE (future));
      graph = dex_graph_builder_build (inner, &error);
      g_assert_no_error (error);
      g_assert_nonnull (graph);
      dex_graph_builder_free (inner);

      future = dex_deferred_new (NULL, run_operation, &operations[0], NULL);
      a = dex_graph_builder_add (outer, DEX_FUTURE (future));
      b = dex_graph_builder_add (outer, graph);
      g_assert_true (dex_graph_builder_add_dependency (outer, b, a));
      future = dex_deferred_new (NULL, run_operation, &operations[2], NULL);
      a = dex_graph_builder_add (outer, DEX_FUTURE (future));
      g_assert_true (dex_graph_builder_add_dependency (outer, a, b));

      graph = dex_graph_builder_build (outer, &error);
      g_assert_no_error (error);
      g_assert_nonnull (graph);
      dex_graph_builder_free (outer);
      graph = dex_future_finally (graph, pass_through, NULL, NULL);
      wait_for (graph);
      assert_graph_result (graph, reject);
      g_assert_cmpuint (calls, ==, reject ? 2 : 3);
      dex_unref (graph);
    }
}

static void
test_graph_invalid_usage (void)
{
  DexGraphBuilder *first = dex_graph_builder_new (NULL);
  DexGraphBuilder *second = dex_graph_builder_new (NULL);
  DexGraphNode *a;
  DexGraphNode *b;
  DexFuture *graph;
  DexFuture *future;
  GError *error = NULL;

  a = dex_graph_builder_add (first, dex_future_new_true ());
  b = dex_graph_builder_add (second, dex_future_new_true ());

  g_test_expect_message ("Dex", G_LOG_LEVEL_CRITICAL, "*node->builder == builder*");
  g_assert_false (dex_graph_builder_add_dependency (first, b, a));
  g_test_assert_expected_messages ();
  g_test_expect_message ("Dex", G_LOG_LEVEL_CRITICAL, "*dependency->builder == builder*");
  g_assert_false (dex_graph_builder_add_dependency (first, a, b));
  g_test_assert_expected_messages ();

  graph = dex_graph_builder_build (first, &error);
  g_assert_no_error (error);
  g_assert_nonnull (graph);

  g_test_expect_message ("Dex", G_LOG_LEVEL_CRITICAL, "*!builder->built*");
  g_assert_null (dex_graph_builder_build (first, &error));
  g_test_assert_expected_messages ();
  g_assert_no_error (error);

  g_test_expect_message ("Dex", G_LOG_LEVEL_CRITICAL, "*!builder->built*");
  g_assert_false (dex_graph_builder_add_dependency (first, a, a));
  g_test_assert_expected_messages ();

  future = dex_future_new_true ();
  g_test_expect_message ("Dex", G_LOG_LEVEL_CRITICAL, "*!builder->built*");
  g_assert_null (dex_graph_builder_add (first, future));
  g_test_assert_expected_messages ();
  dex_unref (future);

  dex_graph_builder_free (first);
  dex_graph_builder_free (second);
  graph = dex_future_finally (graph, pass_through, NULL, NULL);
  wait_for (graph);
  assert_graph_result (graph, FALSE);
  dex_unref (graph);
}

static DexFuture *
discard_graph (gpointer data)
{
  dex_unref (data);
  return dex_future_new_true ();
}

typedef struct
{
  GMutex   mutex;
  GCond    cond;
  guint    waiting;
  gboolean released;
} ThreadGate;

static void
thread_gate_init (ThreadGate *gate)
{
  g_mutex_init (&gate->mutex);
  g_cond_init (&gate->cond);
  gate->waiting = 0;
  gate->released = FALSE;
}

static void
thread_gate_wait (ThreadGate *gate)
{
  gint64 deadline = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;

  g_mutex_lock (&gate->mutex);
  gate->waiting++;
  g_cond_broadcast (&gate->cond);
  while (!gate->released)
    {
      if (!g_cond_wait_until (&gate->cond, &gate->mutex, deadline))
        g_assert_true (gate->released);
    }
  g_mutex_unlock (&gate->mutex);
}

static void
thread_gate_release (ThreadGate *gate,
                     guint       n_threads)
{
  gint64 deadline = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;

  g_mutex_lock (&gate->mutex);
  while (gate->waiting < n_threads)
    {
      if (!g_cond_wait_until (&gate->cond, &gate->mutex, deadline))
        g_assert_cmpuint (gate->waiting, ==, n_threads);
    }
  gate->released = TRUE;
  g_cond_broadcast (&gate->cond);
  g_mutex_unlock (&gate->mutex);
}

static void
thread_gate_clear (ThreadGate *gate)
{
  g_cond_clear (&gate->cond);
  g_mutex_clear (&gate->mutex);
}

static void
complete_promise (DexPromise *promise,
                  gboolean    reject)
{
  if (reject)
    dex_promise_reject (promise,
                        g_error_new_literal (DEX_ERROR,
                                             DEX_ERROR_UNKNOWN,
                                             "operation failed"));
  else
    dex_promise_resolve_boolean (promise, TRUE);
}

typedef struct
{
  ThreadGate *gate;
  DexPromise *promise;
  gboolean    reject;
} ThreadCompletion;

static gpointer
complete_on_thread (gpointer data)
{
  ThreadCompletion *completion = data;

  thread_gate_wait (completion->gate);
  complete_promise (completion->promise, completion->reject);

  return NULL;
}

static DexFuture *
count_completion (DexFuture *future,
                  gpointer   data)
{
  int *calls = data;

  g_atomic_int_inc (calls);
  return dex_ref (future);
}

static void
test_graph_concurrent_completions (void)
{
  for (guint mode = 0; mode < 3; mode++)
    {
      for (guint iteration = 0; iteration < 16; iteration++)
        {
          DexGraphBuilder *builder = dex_graph_builder_new (NULL);
          ThreadGate gate;
          ThreadCompletion completions[8];
          GThread *threads[8];
          TrackedOperation branches[8] = { 0 };
          TrackedOperation join = { 0 };
          DexGraphNode *target;
          DexDeferred *future;
          DexFuture *graph;
          GError *error = NULL;
          int completed = 0;

          thread_gate_init (&gate);
          future = dex_deferred_new (NULL, run_tracked_operation, &join, destroy_tracked_operation);
          target = dex_graph_builder_add (builder, DEX_FUTURE (future));

          for (guint i = 0; i < G_N_ELEMENTS (completions); i++)
            {
              DexGraphNode *root;
              DexGraphNode *branch;

              completions[i] = (ThreadCompletion) {
                &gate, dex_promise_new (), mode == 2 || (mode == 1 && i % 2 == 0)
              };
              root = dex_graph_builder_add (builder, dex_ref (DEX_FUTURE (completions[i].promise)));
              future = dex_deferred_new (NULL, run_tracked_operation,
                                         &branches[i], destroy_tracked_operation);
              branch = dex_graph_builder_add (builder, DEX_FUTURE (future));
              g_assert_true (dex_graph_builder_add_dependency (builder, branch, root));
              g_assert_true (dex_graph_builder_add_dependency (builder, target, root));
            }

          graph = dex_graph_builder_build (builder, &error);
          g_assert_no_error (error);
          g_assert_nonnull (graph);
          dex_graph_builder_free (builder);
          graph = dex_future_finally (graph, count_completion, &completed, NULL);
          drain_context ();
          g_assert_true (dex_future_is_pending (graph));

          for (guint i = 0; i < G_N_ELEMENTS (threads); i++)
            threads[i] = g_thread_new ("graph-complete", complete_on_thread, &completions[i]);

          /* Release all native threads together so completion races within
           * the same dependency gate, including competing rejections.
           */
          thread_gate_release (&gate, G_N_ELEMENTS (threads));
          for (guint i = 0; i < G_N_ELEMENTS (threads); i++)
            g_thread_join (threads[i]);

          wait_for (graph);
          assert_graph_result (graph, mode != 0);
          g_assert_cmpint (g_atomic_int_get (&completed), ==, 1);
          dex_unref (graph);
          drain_context ();

          g_assert_cmpuint (join.calls, ==, mode == 0 ? 1 : 0);
          g_assert_cmpuint (join.destroyed, ==, 1);
          for (guint i = 0; i < G_N_ELEMENTS (branches); i++)
            {
              g_assert_cmpuint (branches[i].calls, ==, completions[i].reject ? 0 : 1);
              g_assert_cmpuint (branches[i].destroyed, ==, 1);
              dex_unref (completions[i].promise);
            }
          thread_gate_clear (&gate);
        }
    }
}

typedef struct
{
  ThreadGate *gate;
  DexFuture  *graph;
} ThreadDiscard;

static gpointer
discard_on_thread (gpointer data)
{
  ThreadDiscard *discard = data;

  thread_gate_wait (discard->gate);
  dex_clear (&discard->graph);

  return NULL;
}

static void
test_graph_discard_race (void)
{
  for (guint reject = 0; reject < 2; reject++)
    {
      for (guint iteration = 0; iteration < 64; iteration++)
        {
          DexGraphBuilder *builder = dex_graph_builder_new (NULL);
          DexPromise *root = dex_promise_new_cancellable ();
          TrackedOperation dependent = { 0 };
          ThreadGate gate;
          ThreadCompletion completion = { &gate, root, reject };
          ThreadDiscard discard = { &gate, NULL };
          DexGraphNode *a;
          DexGraphNode *b;
          DexDeferred *future;
          DexFuture *graph;
          DexFuture *survivor = NULL;
          GThread *resolver;
          GThread *discarder;
          GError *error = NULL;
          int completed = 0;
          int survivor_completed = 0;

          thread_gate_init (&gate);
          a = dex_graph_builder_add (builder, dex_ref (DEX_FUTURE (root)));
          future = dex_deferred_new (NULL,
                                     run_tracked_operation,
                                     &dependent,
                                     destroy_tracked_operation);
          b = dex_graph_builder_add (builder, DEX_FUTURE (future));
          g_assert_true (dex_graph_builder_add_dependency (builder, b, a));
          graph = dex_graph_builder_build (builder, &error);
          g_assert_no_error (error);
          g_assert_nonnull (graph);
          dex_graph_builder_free (builder);
          if (iteration % 2 != 0)
            survivor = dex_future_finally (dex_ref (graph), count_completion,
                                            &survivor_completed, NULL);
          discard.graph = dex_future_finally (graph, count_completion, &completed, NULL);
          drain_context ();
          g_assert_true (dex_future_is_pending (discard.graph));

          resolver = g_thread_new ("graph-resolve", complete_on_thread, &completion);
          discarder = g_thread_new ("graph-discard", discard_on_thread, &discard);
          thread_gate_release (&gate, 2);
          g_thread_join (resolver);
          g_thread_join (discarder);
          drain_context ();

          if (survivor != NULL)
            {
              wait_for (survivor);
              assert_graph_result (survivor, reject);
              g_assert_cmpint (g_atomic_int_get (&survivor_completed), ==, 1);
              g_assert_cmpuint (dependent.calls, ==, reject ? 0 : 1);
              dex_unref (survivor);
              drain_context ();
            }

          /* Either completion or discard may win. Work already queued by a
           * successful completion may run, but must keep its closure alive.
           */
          g_assert_cmpuint (dependent.calls, <=, reject ? 0 : 1);
          g_assert_cmpuint (dependent.destroyed, ==, 1);
          g_assert_cmpint (g_atomic_int_get (&completed), <=, 1);
          g_assert_null (discard.graph);
          dex_unref (root);
          thread_gate_clear (&gate);
        }
    }
}

static void
test_graph_discard_queued (void)
{
  for (guint reject = 0; reject < 2; reject++)
    {
      DexGraphBuilder *builder = dex_graph_builder_new (NULL);
      DexPromise *root = dex_promise_new ();
      PendingOperation dependent = { dex_promise_new_cancellable (), 0, 0 };
      ThreadGate gate;
      ThreadCompletion completion = { &gate, root, reject };
      DexGraphNode *a;
      DexGraphNode *b;
      DexDeferred *future;
      DexFuture *graph;
      GThread *resolver;
      GError *error = NULL;

      thread_gate_init (&gate);
      a = dex_graph_builder_add (builder, dex_ref (DEX_FUTURE (root)));
      future = dex_deferred_new (NULL,
                                 run_pending_operation,
                                 &dependent,
                                 destroy_pending_operation);
      b = dex_graph_builder_add (builder, DEX_FUTURE (future));
      g_assert_true (dex_graph_builder_add_dependency (builder, b, a));
      graph = dex_graph_builder_build (builder, &error);
      g_assert_no_error (error);
      g_assert_nonnull (graph);
      dex_graph_builder_free (builder);
      graph = dex_future_finally (graph, pass_through, NULL, NULL);
      drain_context ();

      /* Completing on another thread queues the gate's callback. Keep the
       * main context stopped until the last graph observer is discarded.
       */
      resolver = g_thread_new ("graph-queue", complete_on_thread, &completion);
      thread_gate_release (&gate, 1);
      g_thread_join (resolver);
      g_assert_true (dex_future_is_pending (graph));
      g_assert_cmpuint (dependent.calls, ==, 0);
      g_assert_cmpuint (dependent.destroyed, ==, 0);
      dex_unref (graph);
      drain_context ();

      g_assert_cmpuint (dependent.calls, <=, reject ? 0 : 1);
      g_assert_cmpuint (dependent.destroyed, ==, 1);
      if (dependent.calls != 0)
        g_assert_true (g_cancellable_is_cancelled (dex_promise_get_cancellable (dependent.promise)));

      complete_promise (dependent.promise, reject);
      drain_context ();
      g_assert_cmpuint (dependent.destroyed, ==, 1);
      dex_unref (dependent.promise);
      dex_unref (root);
      thread_gate_clear (&gate);
    }
}

static void
test_graph_shared_across_graphs (void)
{
  for (guint reject = 0; reject < 2; reject++)
    {
      for (guint observers = 0; observers < 4; observers++)
        {
          PendingOperation operation = { dex_promise_new_cancellable (), 0, 0 };
          TrackedOperation dependents[2] = { 0 };
          DexDeferred *shared;
          DexFuture *graphs[2];
          DexFuture *external = NULL;
          GError *error = NULL;
          gboolean keep_graph = (observers & 1) != 0;
          gboolean keep_external = (observers & 2) != 0;

          shared = dex_deferred_new (NULL,
                                     run_pending_operation,
                                     &operation,
                                     destroy_pending_operation);
          for (guint i = 0; i < G_N_ELEMENTS (graphs); i++)
            {
              DexGraphBuilder *builder = dex_graph_builder_new (NULL);
              DexGraphNode *root;
              DexGraphNode *dependent;
              DexDeferred *future;

              root = dex_graph_builder_add (builder, dex_ref (DEX_FUTURE (shared)));
              future = dex_deferred_new (NULL, run_tracked_operation,
                                         &dependents[i], destroy_tracked_operation);
              dependent = dex_graph_builder_add (builder, DEX_FUTURE (future));
              g_assert_true (dex_graph_builder_add_dependency (builder, dependent, root));
              graphs[i] = dex_graph_builder_build (builder, &error);
              g_assert_no_error (error);
              g_assert_nonnull (graphs[i]);
              dex_graph_builder_free (builder);
              graphs[i] = dex_future_finally (graphs[i], pass_through, NULL, NULL);
            }
          if (keep_external)
            external = dex_future_finally (dex_ref (DEX_FUTURE (shared)), pass_through, NULL, NULL);
          dex_unref (shared);
          drain_context ();
          g_assert_cmpuint (operation.calls, ==, 1);

          dex_clear (&graphs[0]);
          drain_context ();
          g_assert_false (g_cancellable_is_cancelled (dex_promise_get_cancellable (operation.promise)));
          g_assert_cmpuint (dependents[0].calls, ==, 0);
          g_assert_cmpuint (dependents[0].destroyed, ==, 1);
          if (!keep_graph)
            dex_clear (&graphs[1]);
          drain_context ();

          if (keep_graph || keep_external)
            {
              g_assert_false (g_cancellable_is_cancelled (dex_promise_get_cancellable (operation.promise)));
              g_assert_cmpuint (operation.destroyed, ==, 0);
            }
          else
            {
              g_assert_true (g_cancellable_is_cancelled (dex_promise_get_cancellable (operation.promise)));
              g_assert_cmpuint (operation.destroyed, ==, 1);
            }

          complete_promise (operation.promise, reject);
          if (keep_graph)
            {
              wait_for (graphs[1]);

              assert_graph_result (graphs[1], reject);
              dex_clear (&graphs[1]);
            }

          if (keep_external)
            {
              wait_for (external);
              if (reject)
                {
                  g_assert_null (dex_future_get_value (external, &error));
                  g_assert_error (error, DEX_ERROR, DEX_ERROR_UNKNOWN);
                  g_clear_error (&error);
                }
              else
                {
                  assert_graph_result (external, FALSE);
                }
              dex_clear (&external);
            }

          drain_context ();
          g_assert_cmpuint (operation.calls, ==, 1);
          g_assert_cmpuint (operation.destroyed, ==, 1);
          g_assert_cmpuint (dependents[0].calls, ==, 0);
          g_assert_cmpuint (dependents[1].calls, ==, keep_graph && !reject ? 1 : 0);
          g_assert_cmpuint (dependents[1].destroyed, ==, 1);
          dex_unref (operation.promise);
        }
    }
}

static void
test_graph_deep_discard (void)
{
  const guint n_nodes = 1024;

  for (guint reverse = 0; reverse < 2; reverse++)
    {
      DexGraphBuilder *builder = dex_graph_builder_new (NULL);
      DexPromise *root = dex_promise_new_cancellable ();
      DexGraphNode **nodes = g_new (DexGraphNode *, n_nodes);
      TrackedOperation *operations = g_new0 (TrackedOperation, n_nodes);
      DexFuture *graph;
      DexFuture *discarded;
      GError *error = NULL;

      for (guint i = 0; i < n_nodes; i++)
        {
          guint index = reverse ? n_nodes - i - 1 : i;
          DexFuture *future;

          if (index == 0)
            future = dex_ref (DEX_FUTURE (root));
          else
            future = DEX_FUTURE (dex_deferred_new (NULL, run_tracked_operation,
                                                  &operations[index], destroy_tracked_operation));
          nodes[index] = dex_graph_builder_add (builder, future);
        }

      for (guint i = 1; i < n_nodes; i++)
        g_assert_true (dex_graph_builder_add_dependency (builder, nodes[i], nodes[i - 1]));

      graph = dex_graph_builder_build (builder, &error);
      g_assert_no_error (error);
      g_assert_nonnull (graph);
      dex_graph_builder_free (builder);
      graph = dex_future_finally (graph, pass_through, NULL, NULL);
      drain_context ();
      g_assert_true (dex_future_is_pending (graph));

      /* The test harness has a large stack; teardown must also be safe on
       * the default fiber stack used by applications.
       */
      discarded = dex_scheduler_spawn (NULL,
                                       0,
                                       discard_graph,
                                       graph,
                                       NULL);
      wait_for (discarded);
      g_assert_true (dex_future_is_resolved (discarded));
      dex_unref (discarded);
      drain_context ();

      g_assert_true (g_cancellable_is_cancelled (dex_promise_get_cancellable (root)));
      for (guint i = 1; i < n_nodes; i++)
        {
          g_assert_cmpuint (operations[i].calls, ==, 0);
          g_assert_cmpuint (operations[i].destroyed, ==, 1);
        }

      dex_promise_resolve_boolean (root, TRUE);
      drain_context ();
      dex_unref (root);
      g_free (nodes);
      g_free (operations);
    }
}

int
main (int   argc,
      char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Dex/Graph/order", test_graph_order);
  g_test_add_func ("/Dex/Graph/cycle", test_graph_cycle);
  g_test_add_func ("/Dex/Graph/rejection", test_graph_rejection);
  dex_test_add_func ("/Dex/Graph/completed_rejection", test_graph_completed_rejection);
  g_test_add_func ("/Dex/Graph/pending_rejection", test_graph_pending_rejection);
  dex_test_add_func ("/Dex/Graph/shared_dependency", test_graph_shared_dependency);
  g_test_add_func ("/Dex/Graph/empty", test_graph_empty);
  g_test_add_func ("/Dex/Graph/many_dependencies", test_graph_many_dependencies);
  dex_test_add_func ("/Dex/Graph/deep_chain", test_graph_deep_chain);
  g_test_add_func ("/Dex/Graph/dag_combinations", test_graph_dag_combinations);
  g_test_add_func ("/Dex/Graph/pending_fan_in", test_graph_pending_fan_in);
  dex_test_add_func ("/Dex/Graph/wide_rejection", test_graph_wide_rejection);
  g_test_add_func ("/Dex/Graph/lifetime", test_graph_lifetime);
  g_test_add_func ("/Dex/Graph/cycle_variants", test_graph_cycle_variants);
  dex_test_add_func ("/Dex/Graph/schedulers", test_graph_schedulers);
  g_test_add_func ("/Dex/Graph/shared_operation", test_graph_shared_operation);
  g_test_add_func ("/Dex/Graph/discard", test_graph_discard);
  g_test_add_func ("/Dex/Graph/nested", test_graph_nested);
  g_test_add_func ("/Dex/Graph/invalid_usage", test_graph_invalid_usage);
  g_test_add_func ("/Dex/Graph/deep_discard", test_graph_deep_discard);
  g_test_add_func ("/Dex/Graph/concurrent_completions", test_graph_concurrent_completions);
  g_test_add_func ("/Dex/Graph/discard_race", test_graph_discard_race);
  g_test_add_func ("/Dex/Graph/discard_queued", test_graph_discard_queued);
  g_test_add_func ("/Dex/Graph/shared_across_graphs", test_graph_shared_across_graphs);
  return g_test_run ();
}
