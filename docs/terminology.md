Title: Terminology

# Futures

A future is a container that will eventually contain a result or an error.

Programmers often use the words "future" and "promise" interchangeably.
Libdex tries, when possible, to follow the academic nomenclature for futures.
That is to say that a future is the interface and promise is a type of future.

Futures exist in one of three states.
The first state is [enum@Dex.FutureStatus.pending].
A future exists in this state until it has either rejected or resolved.

The second state is [enum@Dex.FutureStatus.resolved].
A future reaches this state when it has successfully obtained a value.

The last third state is [enum@Dex.FutureStatus.rejected].
If there was a failure to obtain a value a future will be in this state and contain a [struct@GLib.Error] representing such failure.

# Promises and More

A promise is a type of future that allows the creator to set the resolved value or error.
This is a common type of future to use when you are integrating with things that are not yet integrated with Libdex.

Other types of futures also exist.

```c
/* resolve to "true" */
DexPromise *good = dex_promise_new ();
dex_promise_resolve_boolean (good, TRUE);

/* reject with error */
DexPromise *bad = dex_promise_new ();
dex_promise_reject (good,
                    g_error_new (G_IO_ERROR,
                                 G_IO_ERROR_FAILED,
                                 "Failed"));
```

## Static Futures

Sometimes you already know the result of a future upfront.
The [class@Dex.StaticFuture] is used in this case.
Various constructors for [class@Dex.Future] will help you create one.

For example, [ctor@Dex.Future.new_take_object] will create a static future for a [class@GObject.Object]-derived instance.

```c
DexFuture *future = dex_future_new_for_int (123);
```

## Blocks

One of the most commonly used types of futures in Libdex is a [class@Dex.Block].

a [class@Dex.Block] is a callback that is run to process the result of a future.
The block itself is also a future meaning that you can chain these blocks together into robust processing groups.

### "Then" Blocks

The first type of block is a "then" block which is created using [ctor@Dex.Future.then].
These blocks will only be run if the dependent future resolves with a value.
Otherwise, the rejection of the dependent future is propagated to the block.

```c
static DexFuture *
further_processing (DexFuture *future,
                    gpointer   user_data)
{
  const GValue *result = dex_promise_get_value (future, NULL);

  return dex_ref (future);
}
```

### "Catch" Blocks

Since some futures may fail, there is value in being able to "catch" the failure and resolve it.
Use [ctor@Dex.Future.catch] to handle the result of a rejected future and resolve or reject it further.

```c
static DexFuture *
catch_rejection (DexFuture *future,
                 gpointer   user_data)
{
  g_autoptr(GError) error = NULL;

  dex_future_get_value (future, &error);

  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    return dex_future_new_true ();

  return dex_ref (future);
}
```

### "Finally" Blocks

There may be times when you want to handle completion of a future whether it resolved or rejected.
For this case, use a "finally" block by calling [ctor@Dex.Future.finally].

### Infinite Loops

If you find you have a case where you want a [class@Dex.Block] to loop indefinitely, you can use the "_loop" variants of the block APIs.

See [ctor@Dex.Future.then_loop], [ctor@Dex.Future.catch_loop], or [ctor@Dex.Future.finally_loop].
This is generally useful when your block's callback will begin the next stage of work as the result of the callback.

## Cancellable

Many programmers who use GTK and GIO are familiar with [class@Gio.Cancellable].
Libdex has something similar in the form of [class@Dex.Cancellable].
However, in the Libdex case, [class@Dex.Cancellable] is a future.

It allows for convenient grouping with other futures to perform cancellation when the [method@Dex.Cancellable.cancel] method is called.

It can also integrate with [class@Gio.Cancellable] when created using [ctor@Dex.Cancellable.new_from_cancellable].

A [class@Dex.Cancellable] will only ever reject.

```c
DexFuture *future = dex_cancellable_new ();

dex_cancellable_cancel (DEX_CANCELLABLE (future));
```

## Timeouts

A timeout may be represented as a future.
In this case, the timeout will reject after a time period has passed.

A [class@Dex.Timeout] will only ever reject.

```c
DexFuture *future = dex_timeout_new_seconds (60);
```

## Unix Signals

Libdex can represent unix signals as a future.
That is to say that the future will resolve to an integer of the signal number when that signal has been raised.

This is implemented using [func@GLibUnix@signal_source_new] and comes with all the same restrictions.

## Delayed

Sometimes you may run into a case where you want to gate the result of a future until a specific moment.

For this case, [class@Dex.Delayed] allows you to wrap another future and decide when to "uncork" the result.

```c
DexFuture *delayed = dex_delayed_new (dex_future_new_true ());

dex_delayed_release (DEX_DELAYED (delayed));
```

## Fibers

Another type of future is a "fiber".

More care will be spent on fibers later on but suffice to say that the result of a fiber is easily consumable as a future via [class@Dex.Fiber].

```c
DexFuture *future = dex_scheduler_spawn (NULL, 0, my_fiber, state, state_free);
```

# Schedulers

Libdex requires much processing that needs to be done on the main loop of a thread.
This is generally handled by a [class@Dex.Scheduler].

The main thread of an application has the default sheduler which is a [class@Dex.MainScheduler].

Libdex also has a managed thread pool of schedulers via the [class@Dex.ThreadPoolScheduler].

Schedulers manage short tasks, executing [class@Dex.Block] when they are ready, finalizing objects on their owning thread, and running fibers.

# Channels

Channels are a higher-level construct built on futures that allow passing work between producers and consumers.
They are akin to Go channels in that they have a read and a write side.
However, they are much more focused on integrating well with [class@Dex.Future].
