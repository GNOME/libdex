Title: Task Groups

# Task Groups

`DexTaskGroup` provides a structured scope for related futures.

Task groups track existing [class@Dex.Future] instances as a synthetic scope.
The task group can track various futures and then be closed. If the task
group is cancelled, all tasks within the group are cancelled.

Tasks groups can be composed of additional task groups as they are themselves a
[class@Dex.Future].

## Core Rules

- Add futures explicitly with [method@Dex.TaskGroup.add].
- Call [method@Dex.TaskGroup.close] when no more futures should be added.
- Call [method@Dex.TaskGroup.cancel] to force cancellation of all tracked
  children.
- The group itself is a [class@Dex.Future] and can be awaited.

## Cancellation

Task group cancellation is stronger than ordinary discard-driven cancellation.

Libdex futures already use discard as an escape hatch when the last observer
goes away. That remains unchanged. A task group can also force cancellation of
its tracked futures even if they are still referenced elsewhere.

To make this safe, futures that are cancelled by a task group are completed
with cancellation before any later resolve/reject attempt can take effect.
Subsequent completion attempts are ignored.

This is the mechanism that lets child code keep running briefly without being
able to resurrect a cancelled future.

## Disowning Futures

[method@Dex.Future.disown] still means "do not cancel this work just because
observers disappeared."

It does not override task-group ownership. If a future is part of a task
group, the group may still cancel it directly.

That keeps the two concepts separate:

- `disown` is about observer interest.
- task groups are about structured ownership.

## Thread Default Groups

Task groups can be pushed as the thread-default group using
[method@Dex.TaskGroup.push_thread_default] and later restored with
[method@Dex.TaskGroup.pop_thread_default].

This is a convenience layer for callers of [method@Dex.TaskGroup.add].
You can pass `null` for the group and it will automatically use the
thread default.

## Example

```c
DexTaskGroup *group = dex_task_group_new ();
DexFuture *group_future;

dex_task_group_add (group, load_config ());
dex_task_group_add (group, refresh_cache ());

group_future = dex_task_group_close (group);

if (!dex_await_boolean (group_future, &error))
  ...
```
