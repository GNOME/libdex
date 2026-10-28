Title: Deferred Work

[class@Dex.Deferred] represents work that starts when another future observes
it. This allows you to create and pass around a future before deciding whether
the work is needed.

For example, an application might create an attachment list when opening a
document. Each attachment can have a future for its contents without reading
every file. Opening an attachment observes that future and starts the read.
Attachments which are never opened do not need to be read at all.

The work starts once. Multiple consumers of the same deferred share the same
operation and its result, including any error.

Deferred work is available since libdex 1.3.

## Preparing Optional Work

Calling [func@Dex.file_load_contents_bytes] starts a read immediately. To make
that read optional, put the call inside a [callback@Dex.DeferredFunc] and use
[ctor@Dex.Deferred.new] to create the future.

```c
static DexFuture *
load_attachment (gpointer user_data)
{
  GFile *file = user_data;

  g_assert (G_IS_FILE (file));

  return dex_file_load_contents_bytes (file);
}

static DexFuture *
prepare_attachment (GFile *file)
{
  g_assert (G_IS_FILE (file));

  return DEX_FUTURE (dex_deferred_new (NULL,
                                       load_attachment,
                                       g_object_ref (file),
                                       g_object_unref));
}
```

The attachment can store the future returned by `prepare_attachment()` until
its contents are needed. The deferred holds a reference to `file`, so the
caller may release its own reference after preparing the attachment.

When a fiber needs the contents, it can await the stored future.

```c
g_autoptr(GBytes) contents = NULL;
g_autoptr(GError) error = NULL;

if (!(contents = dex_await_boxed (dex_ref (attachment_contents), &error)))
  return dex_future_new_for_error (g_steal_pointer (&error));
```

Here `attachment_contents` is the future stored on the attachment.
[method@Dex.Future.await_boxed] takes ownership of its argument, so
`dex_ref()` leaves the attachment's reference in place. Another consumer can
await the same future and receive the same contents without starting a second
read.

The callback returns a new reference to a future. The deferred resolves or
rejects with that future's result. Returning `NULL` from a deferred callback
rejects with [error@Dex.Error.NO_RESULT].

## Observing a Deferred

Awaiting a deferred from a fiber observes it. So does attaching a callback with
[ctor@Dex.Future.then], [ctor@Dex.Future.catch], or [ctor@Dex.Future.finally].
Adding it to a future set or a [class@Dex.TaskGroup] also observes it.
These operations can start the work as soon as they are called; you do not
need to await the resulting future first.

Taking a reference or querying the status does not observe the deferred.
[method@Dex.Future.get_value] does not start the work either. An unstarted
deferred remains pending, even if you iterate the main context.

This distinction matters when preparing optional work. Attaching a logging
callback during attachment creation would start the read. Add that callback
when you actually want to consume the result.

[method@Dex.Future.disown] observes the deferred and keeps it running without
a caller retaining the result. Use it when you have decided that the work
should complete even if its consumers go away.

## Choosing Where Work Runs

[ctor@Dex.Deferred.new] runs a short callback on the selected scheduler. The
callback can start an asynchronous operation and return its future, as in the
attachment example. It does not run on a fiber and cannot await pending work.

Other constructors defer the creation or submission of different kinds of
work.

| Constructor | Work |
| --- | --- |
| [ctor@Dex.Deferred.new_fiber] | A fiber which can use `dex_await()` |
| [ctor@Dex.Deferred.new_coroutine] | A stackless coroutine |
| [ctor@Dex.Deferred.new_thread] | Blocking work on a dedicated thread |
| [ctor@Dex.Deferred.new_on_thread_pool] | Blocking work submitted to a `DexThreadPool` |

For constructors taking a scheduler, passing `NULL` captures the current
thread's scheduler at construction. If the thread has no scheduler, the
process default scheduler is used. Observing the future from another thread
does not change where the work runs.

## Lifetime and Cancellation

If the deferred is released without ever being observed, its destroy notify
releases the callback data without invoking the callback. This is what makes
it useful for attachments which are never opened.

If the last observer is discarded before the work starts, the deferred
rejects with `G_IO_ERROR_CANCELLED`. Once work has started, discarding its
observers propagates to the underlying future. Work may still run while
cancellation propagates. Native thread work which has started continues, and
its callback data remains valid until it completes.

A reference keeps the deferred alive, but does not count as an observer.
Keeping a reference on the attachment therefore does not prevent cancellation
when its last consumer goes away. A deferred starts at most once; create a
new deferred if the application needs to retry a cancelled or failed read.

## Related Features

Deferred work also allows a [dependency graph](graphs.html) to hold operations
without starting them until their prerequisites have resolved.
