#include "config.h"

#include <gio/gio.h>

#include "dex-error.h"
#include "dex-task-group.h"
#include "dex-future-private.h"
#include "dex-thread-storage-private.h"

typedef struct _DexTaskGroup
{
  DexFuture     parent_instance;
  GQueue        futures;
  DexScheduler *scheduler;
  GError       *first_error;
  gpointer      prev_thread_default;
  DexTaskGroupFlags flags;
  guint         n_pending;
  guint         n_resolved;
  guint         n_rejected;
  guint         closed : 1;
  guint         cancelled : 1;
  guint         pushed : 1;
} DexTaskGroup;

typedef struct _DexTaskGroupClass
{
  DexFutureClass parent_class;
} DexTaskGroupClass;

/**
 * DexTaskGroup:
 *
 * A structured scope for related [class@Dex.Future] instances.
 *
 * A task group owns the futures added to it until the group is closed or
 * cancelled. The group itself is also a [class@Dex.Future], so callers can
 * await the scope to learn when all tracked futures have finished.
 *
 * Use [method@Dex.TaskGroup.add] to attach futures explicitly, or pass `NULL`
 * to [method@Dex.TaskGroup.add] after pushing a thread-default group with
 * [method@Dex.TaskGroup.push_thread_default]. Close the group with
 * [method@Dex.TaskGroup.close] once no more futures will be added.
 *
 * Cancellation is stronger than ordinary discard-driven cleanup: calling
 * [method@Dex.TaskGroup.cancel] cancels all tracked children, including nested
 * task groups, and completes the task group with a cancellation error. When
 * [method@Dex.TaskGroup.close] is used instead, the task group resolves only
 * after every tracked future has resolved or rejected.
 *
 * Developer note: always pair [method@Dex.TaskGroup.push_thread_default] with
 * [method@Dex.TaskGroup.pop_thread_default] on the same thread, and prefer
 * [method@Dex.TaskGroup.close] over relying on finalization to finish work.
 *
 * Since: 1.2
 */
DEX_DEFINE_FINAL_TYPE (DexTaskGroup, dex_task_group, DEX_TYPE_FUTURE)

#undef DEX_TYPE_TASK_GROUP
#define DEX_TYPE_TASK_GROUP dex_task_group_type

static void       dex_task_group_cancel_child       (DexTaskGroup *group,
                                                     DexFuture    *future);
static void       dex_task_group_complete_cancelled (DexTaskGroup *group,
                                                     const GError *no_copy,
                                                     GError       *copied);
static void       dex_task_group_discard            (DexFuture    *future);

static const GError *static_cancelled;

static gboolean
dex_task_group_propagate (DexFuture *future,
                          DexFuture *completed)
{
  DexTaskGroup *group = DEX_TASK_GROUP (future);
  gboolean should_complete = FALSE;
  gboolean should_cancel_pending = FALSE;
  gboolean should_discard_child = FALSE;
  gboolean resolved = FALSE;
  GError *error = NULL;
  dex_object_lock (group);

  if (group->n_pending > 0)
    {
      group->n_pending--;
      should_discard_child = TRUE;

      if (!group->cancelled)
        {
          if (dex_future_is_resolved (completed))
            group->n_resolved++;
          else
            {
              group->n_rejected++;

              if (group->first_error == NULL)
                {
                  dex_future_get_value (completed, &group->first_error);
                  if (group->first_error != NULL)
                    error = g_error_copy (group->first_error);
                }

              if ((group->flags & DEX_TASK_GROUP_FLAGS_CANCEL_ON_ERROR) != 0 &&
                  !group->closed)
                {
                  group->closed = TRUE;
                  group->cancelled = TRUE;
                  should_cancel_pending = TRUE;
                }
            }
        }

      if (group->closed && group->n_pending == 0)
        {
          should_complete = TRUE;
          resolved = (group->n_rejected == 0);
          if (!resolved && group->first_error != NULL && error == NULL)
            error = g_error_copy (group->first_error);
        }
    }

  dex_object_unlock (group);

  if (should_discard_child)
    {
      g_queue_unlink (&group->futures, &completed->task_group_link);
      dex_future_discard (completed, DEX_FUTURE (group));
    }

  if (should_cancel_pending)
    {
      for (GList *iter = group->futures.head; iter != NULL; iter = iter->next)
        dex_task_group_cancel_child (group, iter->data);

      dex_task_group_complete_cancelled (group,
                                         static_cancelled,
                                         g_steal_pointer (&error));
      return TRUE;
    }

  if (should_complete)
    {
      if (resolved)
        {
          GValue value = G_VALUE_INIT;
          g_value_init (&value, G_TYPE_BOOLEAN);
          g_value_set_boolean (&value, TRUE);
          dex_future_complete (future, &value, NULL);
          g_value_unset (&value);
        }
      else
        {
          if (error == NULL)
            error = g_error_new_literal (DEX_ERROR,
                                         DEX_ERROR_DEPENDENCY_FAILED,
                                         "One or more task group children failed");
          dex_future_complete (future, NULL, error);
        }
      return TRUE;
    }

  g_clear_error (&error);
  return TRUE;
}

static void
dex_task_group_finalize (DexObject *object)
{
  DexTaskGroup *group = DEX_TASK_GROUP (object);
  DexThreadStorage *storage;

  storage = dex_thread_storage_peek ();
  if (group->pushed && storage != NULL && storage->task_group == group)
    {
      g_critical ("DexTaskGroup %p was finalized while still pushed as the thread-default task group; "
                  "call dex_task_group_pop_thread_default() before unreffing the group",
                  group);
      dex_unref (storage->task_group);
      storage->task_group = group->prev_thread_default;
      group->prev_thread_default = NULL;
      group->pushed = FALSE;
    }

  if (group->futures.head != NULL)
    {
      while (group->futures.head != NULL)
        {
          DexFuture *future = g_queue_peek_head (&group->futures);

          g_queue_unlink (&group->futures, &future->task_group_link);
          dex_future_discard (future, DEX_FUTURE (group));
          dex_unref (future);
        }
    }

  g_clear_error (&group->first_error);

  DEX_OBJECT_CLASS (dex_task_group_parent_class)->finalize (object);
}

static void
dex_task_group_class_init (DexTaskGroupClass *klass)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (klass);
  DexFutureClass *future_class = DEX_FUTURE_CLASS (klass);

  object_class->finalize = dex_task_group_finalize;
  future_class->propagate = dex_task_group_propagate;
  future_class->discard = dex_task_group_discard;

  static_cancelled = g_error_new_literal (G_IO_ERROR,
                                          G_IO_ERROR_CANCELLED,
                                          "Task group was cancelled");
}

static void
dex_task_group_init (DexTaskGroup *group)
{
}

static void
dex_task_group_discard (DexFuture *future)
{
  dex_task_group_cancel (DEX_TASK_GROUP (future));
}

DexTaskGroup *
dex_task_group_new (DexTaskGroupFlags flags)
{
  DexTaskGroup *group = (DexTaskGroup *)dex_object_create_instance (DEX_TYPE_TASK_GROUP);
  group->flags = flags;
  return group;
}

/**
 * dex_task_group_add:
 * @group: (nullable): a [class@Dex.TaskGroup]
 * @future: (transfer full): a [class@Dex.Future]
 *
 * Adds @future to @group.
 *
 * If @group is `null`, then the future will be disowned.
 * Otherwise, @future will be added to @group.
 *
 * Returns: `false` if the group is closed or cancelled;
 *   otherwise `true`.
 *
 * Since: 1.2
 */
gboolean
dex_task_group_add (DexTaskGroup *group,
                    DexFuture    *future)
{
  g_return_val_if_fail (DEX_IS_FUTURE (future), FALSE);

  if (group == NULL)
    {
      DexThreadStorage *storage = dex_thread_storage_peek ();
      group = storage != NULL ? storage->task_group : NULL;

      if (group == NULL)
        {
          dex_future_disown (g_steal_pointer (&future));
          return TRUE;
        }
    }

  g_return_val_if_fail (DEX_IS_TASK_GROUP (group), FALSE);
  g_return_val_if_fail (future != DEX_FUTURE (group), FALSE);

  dex_object_lock (group);
  if (group->closed || group->cancelled)
    {
      dex_object_unlock (group);
      dex_future_disown (g_steal_pointer (&future));
      return FALSE;
    }

  /* take ownership of @future */
  g_queue_push_tail_link (&group->futures, &future->task_group_link);
  group->n_pending++;
  dex_object_unlock (group);
  dex_future_chain (future, DEX_FUTURE (group));

  return TRUE;
}

/**
 * dex_task_group_close:
 * @group: a [class@Dex.TaskGroup]
 *
 * Close the group to new tasks.
 *
 * Returns: (transfer full): a [class@Dex.Future] that resolves when all the
 *   collected futures have resolved or rejected.
 *
 * Since: 1.2
 */
DexFuture *
dex_task_group_close (DexTaskGroup *group)
{
  gboolean resolved = FALSE;
  GError *error = NULL;

  dex_return_error_if_fail (DEX_IS_TASK_GROUP (group));

  dex_object_lock (group);
  group->closed = TRUE;
  if (group->n_pending == 0 && DEX_FUTURE (group)->status == DEX_FUTURE_STATUS_PENDING)
    {
      resolved = (group->n_rejected == 0);
      if (!resolved && group->first_error != NULL)
        error = g_error_copy (group->first_error);
    }
  dex_object_unlock (group);

  if (error != NULL || resolved)
    {
      if (resolved)
        {
          GValue value = G_VALUE_INIT;
          g_value_init (&value, G_TYPE_BOOLEAN);
          g_value_set_boolean (&value, TRUE);
          dex_future_complete (DEX_FUTURE (group), &value, NULL);
          g_value_unset (&value);
        }
      else
        dex_future_complete (DEX_FUTURE (group), NULL, g_steal_pointer (&error));
    }

  return DEX_FUTURE (dex_ref (group));
}

static void
dex_task_group_cancel_child (DexTaskGroup *group,
                             DexFuture    *future)
{
  if (DEX_FUTURE_GET_CLASS (future)->discard != NULL)
    DEX_FUTURE_GET_CLASS (future)->discard (future);

  dex_future_complete (future, NULL, g_error_copy (static_cancelled));
  dex_future_discard (future, DEX_FUTURE (group));
}

static void
dex_task_group_complete_cancelled (DexTaskGroup *group,
                                   const GError *no_copy,
                                   GError       *copied)
{
  g_assert (DEX_IS_TASK_GROUP (group));
  g_assert (no_copy != NULL || copied != NULL);

  if (dex_future_get_status (DEX_FUTURE (group)) == DEX_FUTURE_STATUS_PENDING)
    {
      if (copied != NULL)
        dex_future_complete (DEX_FUTURE (group), NULL, g_steal_pointer (&copied));
      else
        dex_future_complete (DEX_FUTURE (group), NULL, g_error_copy (no_copy));
    }

  g_clear_error (&copied);
}

void
dex_task_group_cancel (DexTaskGroup *group)
{
  g_return_if_fail (DEX_IS_TASK_GROUP (group));

  dex_object_lock (group);
  if (group->cancelled)
    {
      dex_object_unlock (group);
      return;
    }

  group->n_pending = 0;
  group->cancelled = TRUE;
  group->closed = TRUE;

  dex_object_unlock (group);

  for (GList *iter = group->futures.head; iter != NULL; iter = iter->next)
    dex_task_group_cancel_child (group, iter->data);

  dex_task_group_complete_cancelled (group, static_cancelled, NULL);
}

void
dex_task_group_push_thread_default (DexTaskGroup *group)
{
  DexThreadStorage *storage;

  g_return_if_fail (DEX_IS_TASK_GROUP (group));

  storage = dex_thread_storage_get ();

  group->prev_thread_default = storage->task_group;
  storage->task_group = dex_ref (group);
  group->pushed = TRUE;
}

void
dex_task_group_pop_thread_default (DexTaskGroup *group)
{
  DexThreadStorage *storage;

  g_return_if_fail (DEX_IS_TASK_GROUP (group));
  g_return_if_fail (group->pushed);

  storage = dex_thread_storage_get ();
  g_return_if_fail (storage->task_group == group);

  dex_unref (storage->task_group);
  storage->task_group = group->prev_thread_default;
  group->prev_thread_default = NULL;
  group->pushed = FALSE;
}
