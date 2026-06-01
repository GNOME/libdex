#include "config.h"

#include <libdex.h>

typedef struct
{
  guint cancelled_count;
} TimeoutTestState;

typedef struct
{
  GCancellable *cancellable;
} FinalizeTestState;

static DexFuture *
timeout_task_group_fiber (gpointer data)
{
  TimeoutTestState *state = data;
  DexTaskGroup *group = dex_task_group_new (DEX_TASK_GROUP_FLAGS_NONE);
  DexPromise *first = dex_promise_new_cancellable ();
  DexPromise *second = dex_promise_new_cancellable ();
  DexFuture *timed;
  g_autoptr(GError) error = NULL;
  GCancellable *first_cancellable = dex_promise_get_cancellable (first);
  GCancellable *second_cancellable = dex_promise_get_cancellable (second);

  g_assert_true (dex_task_group_add (group, dex_ref (DEX_FUTURE (first))));
  g_assert_true (dex_task_group_add (group, dex_ref (DEX_FUTURE (second))));

  timed = dex_future_with_timeout_msec (dex_task_group_close (group), 10);

  g_assert_false (dex_await_boolean (timed, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_TIMED_OUT);

  g_assert_true (g_cancellable_is_cancelled (first_cancellable));
  g_assert_true (g_cancellable_is_cancelled (second_cancellable));
  state->cancelled_count = 2;

  dex_clear (&first);
  dex_clear (&second);
  dex_clear (&group);

  return dex_future_new_true ();
}

static void
test_task_group_cancel (void)
{
  DexTaskGroup *group = dex_task_group_new (DEX_TASK_GROUP_FLAGS_NONE);
  DexPromise *promise = dex_promise_new_cancellable ();
  GCancellable *cancellable = dex_promise_get_cancellable (promise);
  g_autoptr(GError) error = NULL;
  GValue value = G_VALUE_INIT;

  g_assert_true (dex_task_group_add (group, dex_ref (DEX_FUTURE (promise))));
  dex_task_group_cancel (group);

  g_assert_true (g_cancellable_is_cancelled (cancellable));

  g_value_init (&value, G_TYPE_INT);
  g_value_set_int (&value, 42);
  dex_promise_resolve (promise, &value);
  g_value_unset (&value);

  g_assert_true (dex_future_is_rejected (DEX_FUTURE (promise)));
  g_assert_false (dex_future_is_resolved (DEX_FUTURE (promise)));
  g_assert_null (dex_future_get_value (DEX_FUTURE (promise), &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);

  dex_clear (&promise);
  dex_clear (&group);
}

static void
test_task_group_close_all_resolved (void)
{
  DexTaskGroup *group = dex_task_group_new (DEX_TASK_GROUP_FLAGS_NONE);
  DexFuture *group_future;
  DexPromise *promise = dex_promise_new ();
  GValue value = G_VALUE_INIT;
  g_autoptr(GError) error = NULL;
  const GValue *resolved;

  g_assert_true (dex_task_group_add (group, dex_ref (DEX_FUTURE (promise))));

  g_value_init (&value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&value, TRUE);
  dex_promise_resolve (promise, &value);
  g_value_unset (&value);

  group_future = dex_task_group_close (group);
  resolved = dex_future_get_value (group_future, &error);
  g_assert_no_error (error);
  g_assert_nonnull (resolved);
  g_assert_true (G_VALUE_HOLDS_BOOLEAN (resolved));
  g_assert_true (g_value_get_boolean (resolved));

  dex_clear (&group_future);
  dex_clear (&promise);
  dex_clear (&group);
}

static void
test_task_group_cancel_on_error (void)
{
  DexTaskGroup *group = dex_task_group_new (DEX_TASK_GROUP_FLAGS_CANCEL_ON_ERROR);
  DexPromise *first = dex_promise_new ();
  DexPromise *second = dex_promise_new_cancellable ();
  GCancellable *second_cancellable = dex_promise_get_cancellable (second);
  g_autoptr(GError) error = NULL;

  g_assert_true (dex_task_group_add (group, dex_ref (DEX_FUTURE (first))));
  g_assert_true (dex_task_group_add (group, dex_ref (DEX_FUTURE (second))));

  dex_promise_reject (first, g_error_new_literal (G_IO_ERROR,
                                                  G_IO_ERROR_FAILED,
                                                  "first failed"));

  g_assert_true (g_cancellable_is_cancelled (second_cancellable));

  group = DEX_TASK_GROUP (dex_task_group_close (group));

  g_assert_true (dex_future_is_rejected (DEX_FUTURE (group)));
  g_assert_false (dex_future_is_resolved (DEX_FUTURE (group)));
  g_assert_null (dex_future_get_value (DEX_FUTURE (group), &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);

  dex_clear (&group);
  dex_clear (&second);
  dex_clear (&first);
}

static void
test_task_group_add_self_subprocess (void)
{
  DexTaskGroup *group = dex_task_group_new (DEX_TASK_GROUP_FLAGS_NONE);

  dex_task_group_add (group, dex_ref (DEX_FUTURE (group)));

  dex_clear (&group);
}

static void
test_task_group_add_self (void)
{
  g_test_trap_subprocess ("/dex/task-group/add-self/subprocess",
                          0,
                          G_TEST_SUBPROCESS_DEFAULT);
  g_test_trap_assert_failed ();
  g_test_trap_assert_stderr ("*dex_task_group_add: assertion 'future != DEX_FUTURE (group)' failed*");
}

static void
test_task_group_timeout_cancels_children (void)
{
  GMainContext *context = g_main_context_default ();
  TimeoutTestState state = {0};
  DexFuture *future;
  g_autoptr(GError) error = NULL;
  const GValue *value;

  future = dex_scheduler_spawn (NULL, 0,
                                timeout_task_group_fiber,
                                &state,
                                NULL);

  while (dex_future_is_pending (future))
    g_main_context_iteration (context, TRUE);

  value = dex_future_get_value (future, &error);
  g_assert_no_error (error);
  g_assert_nonnull (value);
  g_assert_true (G_VALUE_HOLDS_BOOLEAN (value));
  g_assert_true (g_value_get_boolean (value));
  g_assert_cmpuint (state.cancelled_count, ==, 2);

  dex_clear (&future);
}

static void
test_task_group_nested_cancellation (void)
{
  DexTaskGroup *outer = dex_task_group_new (DEX_TASK_GROUP_FLAGS_NONE);
  DexTaskGroup *middle = dex_task_group_new (DEX_TASK_GROUP_FLAGS_NONE);
  DexTaskGroup *inner = dex_task_group_new (DEX_TASK_GROUP_FLAGS_NONE);
  DexPromise *leaf = dex_promise_new_cancellable ();
  GCancellable *leaf_cancellable = dex_promise_get_cancellable (leaf);

  g_assert_true (dex_task_group_add (inner, dex_ref (DEX_FUTURE (leaf))));
  g_assert_true (dex_task_group_add (middle, dex_ref (DEX_FUTURE (inner))));
  g_assert_true (dex_task_group_add (outer, dex_ref (DEX_FUTURE (middle))));

  dex_task_group_cancel (outer);

  g_assert_true (g_cancellable_is_cancelled (leaf_cancellable));
  g_assert_true (dex_future_is_rejected (DEX_FUTURE (leaf)));
  g_assert_true (dex_future_is_rejected (DEX_FUTURE (inner)));
  g_assert_true (dex_future_is_rejected (DEX_FUTURE (middle)));
  g_assert_true (dex_future_is_rejected (DEX_FUTURE (outer)));

  dex_clear (&outer);
  dex_clear (&middle);
  dex_clear (&inner);
  dex_clear (&leaf);
}

static void
test_task_group_null_uses_thread_default (void)
{
  DexTaskGroup *group = dex_task_group_new (DEX_TASK_GROUP_FLAGS_NONE);
  DexPromise *promise = dex_promise_new_cancellable ();
  GCancellable *cancellable = dex_promise_get_cancellable (promise);
  g_autoptr(GError) error = NULL;

  dex_task_group_push_thread_default (group);
  g_assert_true (dex_task_group_add (NULL, dex_ref (DEX_FUTURE (promise))));
  dex_task_group_pop_thread_default (group);

  dex_task_group_cancel (group);

  g_assert_true (g_cancellable_is_cancelled (cancellable));
  g_assert_true (dex_future_is_rejected (DEX_FUTURE (promise)));
  g_assert_null (dex_future_get_value (DEX_FUTURE (promise), &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);

  dex_clear (&promise);
  dex_clear (&group);
}

static void
test_task_group_thread_default_nesting (void)
{
  DexTaskGroup *outer = dex_task_group_new (DEX_TASK_GROUP_FLAGS_NONE);
  DexTaskGroup *inner = dex_task_group_new (DEX_TASK_GROUP_FLAGS_NONE);
  DexPromise *outer_promise = dex_promise_new_cancellable ();
  DexPromise *inner_promise = dex_promise_new_cancellable ();
  DexPromise *restored_promise = dex_promise_new_cancellable ();
  GCancellable *inner_cancellable = dex_promise_get_cancellable (inner_promise);
  GCancellable *restored_cancellable = dex_promise_get_cancellable (restored_promise);

  dex_task_group_push_thread_default (outer);
  g_assert_true (dex_task_group_add (NULL, dex_ref (DEX_FUTURE (outer_promise))));
  g_assert_true (dex_task_group_add (NULL, dex_ref (DEX_FUTURE (inner))));

  dex_task_group_push_thread_default (inner);
  g_assert_true (dex_task_group_add (NULL, dex_ref (DEX_FUTURE (inner_promise))));

  dex_task_group_pop_thread_default (inner);
  g_assert_true (dex_task_group_add (NULL, dex_ref (DEX_FUTURE (restored_promise))));

  dex_task_group_pop_thread_default (outer);

  dex_task_group_cancel (outer);

  g_assert_true (g_cancellable_is_cancelled (dex_promise_get_cancellable (outer_promise)));
  g_assert_true (g_cancellable_is_cancelled (inner_cancellable));
  g_assert_true (g_cancellable_is_cancelled (restored_cancellable));

  dex_clear (&restored_promise);
  dex_clear (&inner_promise);
  dex_clear (&outer_promise);
  dex_clear (&inner);
  dex_clear (&outer);
}

static void
test_task_group_finalize_cancels_children (void)
{
  DexPromise *promise = dex_promise_new_cancellable ();
  FinalizeTestState state = {
    .cancellable = dex_promise_get_cancellable (promise),
  };

  {
    DexTaskGroup *group = dex_task_group_new (DEX_TASK_GROUP_FLAGS_NONE);

    g_assert_true (dex_task_group_add (group, dex_ref (DEX_FUTURE (promise))));

    dex_unref (group);
  }

  g_assert_true (g_cancellable_is_cancelled (state.cancellable));

  dex_clear (&promise);
}

int
main (int argc, char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/dex/task-group/cancel", test_task_group_cancel);
  g_test_add_func ("/dex/task-group/close-all-resolved", test_task_group_close_all_resolved);
  g_test_add_func ("/dex/task-group/cancel-on-error", test_task_group_cancel_on_error);
  g_test_add_func ("/dex/task-group/add-self/subprocess", test_task_group_add_self_subprocess);
  g_test_add_func ("/dex/task-group/add-self", test_task_group_add_self);
  g_test_add_func ("/dex/task-group/timeout-cancels-children", test_task_group_timeout_cancels_children);
  g_test_add_func ("/dex/task-group/nested-cancellation", test_task_group_nested_cancellation);
  g_test_add_func ("/dex/task-group/null-uses-thread-default", test_task_group_null_uses_thread_default);
  g_test_add_func ("/dex/task-group/thread-default-nesting", test_task_group_thread_default_nesting);
  g_test_add_func ("/dex/task-group/finalize-cancels-children", test_task_group_finalize_cancels_children);

  return g_test_run ();
}
