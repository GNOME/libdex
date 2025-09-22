Title: Integrating GAsyncResult

Historically if you wanted to do asynchronous work in GObject-based applications you would use [callback@Gio.AsyncReadyCallback] and [iface@Gio.AsyncResult].

There are two ways to integrate with this form of asynchronous API.
In one direction, you can consume this historical API and provide the result as a [class@Dex.Future].
In the other direction, you can provide this API in your application or library but implement it behind the scenes with [class@Dex.Future].

# Converting GAsyncResult to Futures

A typical case to integrate, at least initially, is to extract the result of a [iface@Gio.AsyncResult] and propagate it to a future.

One way to do that is with a [class@Dex.Promise] which will resolve or reject from your async callback.

```c
static void
my_callback (GObject      *object,
             GAsyncResult *result,
             gpointer      user_data)
{
  g_autoptr(DexPromise) promise = user_data;
  g_autoptr(GError) error = NULL;

  if (thing_finish (THING (object), result, &error))
    dex_promise_resolve_boolean (promise, TRUE);
  else
    dex_promise_reject (promise, g_steal_pointer (&error));
}

static DexFuture *
my_wrapper (Thing *thing)
{
  DexPromise *promise = dex_promise_new_cancellable ();

  thing_async (thing,
               dex_promise_get_cancellable (promise),
               my_callback,
               dex_ref (promise));

  return DEX_FUTURE (promise);
}
```

# Implementing AsyncResult with Futures

In some cases you may not want to "leak" into your API that you are using [class@Dex.Future].
For example, you may want to only expose a traditional GIO API or maybe even rewriting legacy code.

For these cases use [class@Dex.AsyncResult].
It is designed to feel familiar to those that have used [class@Gio.Task].

[ctor@Dex.AsyncResult.new] allows taking the typical `cancellable`, `callback`, and `user_data` parameters similar to [class@Gio.Task].

Then call [method@Dex.AsyncResult.await] providing the future that will resolve or reject with error.
One completed, the users provided `callback` will be executed within the active scheduler at time of creation.

From your finish function, call the appropriate propagate API such as [method@Dex.AsyncResult.propagate_int].

```c
void
thing_async (Thing               *thing,
             GCancellable        *cancellable,
             GAsyncReadyCallback  callback,
             gpointer             user_data)
{
  g_autoptr(DexAsyncResult) result = NULL;

  result = dex_async_result_new (thing, cancellable, callback, user_data);
  dex_async_result_await (result, dex_future_new_true ());
}

gboolean
thing_finish (Thing         *thing,
              GAsyncResult  *result,
              GError       **error)
{
  return dex_async_result_propagate_boolean (DEX_ASYNC_RESULT (result), error);
}
```

# Safety Notes

One thing that Libdex handles better than `GTask` is ensuring that your `user_data` is destroyed on the proper thread.
The design of [class@Dex.Block] was done in such a way that both the result and `user_data` are passed back to the owning thread at the same time.
This ensures that your `user_data` will never be finalized on the wrong thread.
