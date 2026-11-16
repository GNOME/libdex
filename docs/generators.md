Title: Generators

# Generators

`DexGenerator` produces futures on demand. A factory runs in the calling context
and returns each future without observing it, so taking a deferred does not
start its work:

```c
static DexFuture *
next_job (gpointer data)
{
  Jobs *jobs = data;

  if (jobs->position == jobs->length)
    return NULL;

  return DEX_FUTURE (dex_deferred_new (NULL, run_job,
                                       job_ref (jobs->items[jobs->position++]),
                                       job_unref));
}
```

Returning `NULL` ends the sequence. A rejected item does not. Factory data stays
alive until close or finalization; returned futures must retain what they need.
Factories cannot run concurrently or recursively.

## Consumption

```c
GError *error = NULL;
int number;

while (dex_generator_await_next_int (generator, &number, &error))
  process_number (number);

if (error != NULL)
  report_error (error);
g_clear_error (&error);
```

The return value indicates whether an item was received. False, zero, and NULL
are valid items. Pending pulls require a fiber to await them.

For manual consumption, await the pull, check its error, then check
`dex_generator_is_exhausted()` before reading the value. Finish each pull before
requesting another: exhaustion cannot classify earlier prefetched items.

The generic `dex_generator_await_next()` takes a `G_VALUE_INIT` output. Unset it
after a successful pull. Other DexObjects can use this path with
`dex_value_set_object()`.

## Fiber producers

```c
static DexFuture *
produce (gpointer data)
{
  GError *error = NULL;

  for (int i = 0; i < 10; i++)
    if (!dex_fiber_yield_int (i, &error))
      return dex_future_new_for_error (error);

  return dex_future_new_true ();
}
```

Use `dex_generator_new_fiber()` to start the producer on its first pull.
Each yield waits for the next pull. Only one pull can be outstanding.
A successful final future ends the sequence; rejection fails subsequent pulls.

`dex_fiber_yield_future()` retains a future without observing it. The consumer's
`_future()` helper awaits the pull, leaving the yielded future untouched. See
`examples/generators.c` for yielding deferred jobs this way.

## Coroutine producers

```c
static DexFuture *
produce_coroutine (DexCoroutineContext *context,
                   gpointer             data)
{
  Producer *state = data;
  GError *error = NULL;

  DEX_COROUTINE_BEGIN (context);
  while (state->position < 10)
    {
      DEX_COROUTINE_YIELD_INT (state->position++, &error);
      if (error != NULL)
        return dex_future_new_for_error (error);
    }
  return dex_future_new_true ();
  DEX_COROUTINE_END;
}
```

Use `dex_generator_new_coroutine()`. Yield expressions run once, and values are
copied before suspension. Keep state in user data across yields.

## Cleanup

Call `dex_generator_close()` when stopping early. It cancels the producer without
waiting and rejects new pulls with `G_IO_ERROR_CLOSED`. Closing does not mark
exhaustion or cancel items already returned by an ordinary factory.

Dropping a pending adapter pull or the generator also closes the producer.
Return after a failed yield or await so cleanup can finish. To wait for the
destroy notify:

```c
dex_await_boolean (dex_generator_close_after_drain (generator), NULL);
```

Drain waits for cleanup, including pending coroutine work that still uses the
producer data. Production errors come through pulls.

Fiber and coroutine producers clean up on their captured scheduler, including
when closed before the first pull. Ordinary factories clean up on the closing
thread, or the thread finishing an active callback.
