Title: Stackless Coroutines

# Stackless Coroutines

Libdex supports two coroutine-like execution models:

* [class@Dex.Fiber] for stackful execution with its own stack.
* [class@Dex.Coroutine] for stackless execution using an explicit state machine.

[class@Dex.Coroutine] is a [class@Dex.Future] that runs on a [class@Dex.Scheduler].
Use it when you want linear async-style flow without paying for a full fiber stack.

## When To Use This Over a Fiber

Use a coroutine when all of these are true:

* The task is mostly "then/await-like" and spends most of its time waiting for futures.
* You want low-overhead scheduling and smaller per-task memory footprint.
* You are comfortable with explicit resume-state style code.

Prefer a [class@Dex.Fiber] when:

* You need deep call stacks, existing recursive control flow, or
  third-party functions that expect normal stackful execution.

## How to Run A Coroutine

Use [method@Dex.Scheduler.spawn_coroutine] to schedule a coroutine.

```c
DexFuture *
my_coroutine (DexCoroutineContext *context,
              gpointer             user_data)
{
  DEX_COROUTINE_BEGIN (context);

  /* return a future when done */
  return dex_future_new_true ();

  DEX_COROUTINE_END;
}

DexFuture *future =
  dex_scheduler_spawn_coroutine (NULL,
                                 my_coroutine,
                                 user_data);
```

If `scheduler` is `NULL`, the default scheduler is used.

## The Function Contract

Coroutine entrypoints use [type@Dex.CoroutineFunc]:

* Return a [class@Dex.Future] to complete.
* Return `NULL` only to indicate that the coroutine is suspended.
* Store any state you need in the provided `user_data`.

The recommended pattern is:

* define any state as a small struct (or with [macro@DEX_DEFINE_CLOSURE_TYPE]);
* call [macro@DEX_COROUTINE_BEGIN] at the start;
* call `DEX_COROUTINE_SUSPEND_*` for each awaited step;
* return a future at completion;
* end with [macro@DEX_COROUTINE_END].

## Waiting on Futures Correctly

Use [macro@DEX_COROUTINE_SUSPEND_BOOLEAN] and related macros instead of calling
`dex_await_*()` directly.

`dex_await_*()` is a fiber-first API and in practice cannot be used like normal
code unless the future is already done. Coroutine suspend helpers:

* save the awaited future in the coroutine state;
* return `NULL` to yield;
* resume execution only after that future settles;
* extract the awaited value on resume.

These macros are available for common await types:

* [macro@DEX_COROUTINE_SUSPEND_BOOLEAN]
* [macro@DEX_COROUTINE_SUSPEND_INT], [macro@DEX_COROUTINE_SUSPEND_UINT]
* [macro@DEX_COROUTINE_SUSPEND_INT64], [macro@DEX_COROUTINE_SUSPEND_UINT64]
* [macro@DEX_COROUTINE_SUSPEND_OBJECT], [macro@DEX_COROUTINE_SUSPEND_BOXED]
* [macro@DEX_COROUTINE_SUSPEND_POINTER]
* [macro@DEX_COROUTINE_SUSPEND_ENUM], [macro@DEX_COROUTINE_SUSPEND_FLAGS]
* [macro@DEX_COROUTINE_SUSPEND_DOUBLE], [macro@DEX_COROUTINE_SUSPEND_FLOAT]

## Common Correctness Pattern

Keep coroutine state explicit.

`DEX_COROUTINE_SUSPEND_*` assumes the state machine stores both the continuation
label and the currently pending future. A typical helper is:

```c
DEX_DEFINE_CLOSURE_TYPE (LoadState, load_state,
                         DEX_DEFINE_CLOSURE_OBJECT (GFile, file),
                         DEX_DEFINE_CLOSURE_OBJECT (GFileInputStream, input),
                         DEX_DEFINE_CLOSURE_OBJECT (GFileInfo, info),
                         DEX_DEFINE_CLOSURE_VALUE (int, io_priority))

static DexFuture *
load_with_cache (DexCoroutineContext *context,
                 gpointer             user_data)
{
  LoadState *state = user_data;
  g_autoptr(GError) error = NULL;

  DEX_COROUTINE_BEGIN (context);

  DEX_COROUTINE_SUSPEND_OBJECT (&state->input, &error,
                                dex_file_read (state->file, state->io_priority));
  if (error != NULL)
    return dex_future_new_for_error (g_steal_pointer (&error));

  DEX_COROUTINE_SUSPEND_OBJECT (&state->info, &error,
                                dex_file_input_stream_query_info (state->input,
                                                                  G_FILE_ATTRIBUTE_STANDARD_SIZE,
                                                                  state->io_priority));
  if (error != NULL)
    return dex_future_new_for_error (g_steal_pointer (&error));

  return dex_future_new_int64 (g_file_info_get_size (state->info));

  DEX_COROUTINE_END;
}
```

## Cleanup And Cancellation

Like all `DexFuture` values, coroutines follow normal ownership and cancellation
rules.

* If a coroutine is no longer depended on, discarding may cancel its work.
* Cancellation will propagate into an error for suspended coroutines which
  they can handle when they are next resumed.
* If work must complete even after callers drop references, use
  [method@Dex.Future.disown] on the returned future.
* State registered with the coroutine is released with the configured
  callback function when the [class@Dex.Coroutine] completes.

## When Not To Use

Do not use coroutines for arbitrary synchronous or blocking loops.
Blocking work should be offloaded with [func@Dex.thread_spawn] or [class@Dex.ThreadPool].

## Related Pages

* [Coroutines API reference](coroutines.html)
* [Scheduler behavior and pinning](schedulers.html)
