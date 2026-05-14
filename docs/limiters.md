Title: Limiters

# Overview

[class@Dex.Limiter] bounds the number of asynchronous operations that may run
at once. Use it when a workload can produce more parallel work than a service,
subsystem, or machine should handle at the same time.

Typical uses include indexing files, downloading URLs, generating thumbnails,
parsing documents, or querying a service with a known concurrency budget.

## Running Work

The safest API is [method@Dex.Limiter.run]. It acquires a permit, spawns a
fiber, and releases the permit when that fiber completes.

```c
static DexFuture *
load_one_file (gpointer user_data)
{
  GFile *file = user_data;

  return dex_file_load_contents_bytes (file);
}

DexLimiter *limiter = dex_limiter_new (8);
DexFuture *future = dex_limiter_run (limiter,
                                     NULL,
                                     0,
                                     load_one_file,
                                     g_object_ref (file),
                                     g_object_unref);
```

The returned future resolves or rejects with the result of the spawned fiber.
If many calls to [method@Dex.Limiter.run] are made, only the configured number
will run at once. Once a fiber has started, dropping the returned future does
not stop the fiber; it is allowed to complete so the permit can be released.

## Manual Scopes

Use [method@Dex.Limiter.acquire] and [method@Dex.Limiter.release] when a permit
must cover a custom scope that is not a single fiber.

```c
g_autoptr(GError) error = NULL;

if (dex_await (dex_limiter_acquire (limiter), &error))
  {
    do_limited_work ();
    dex_limiter_release (limiter);
  }
```

Release exactly once for every successful acquisition. Prefer
[method@Dex.Limiter.run] when possible because it handles release on both
success and failure paths.

## Choosing a Limit

Choose a limit that matches the constrained resource, not the number of items
in the queue.

- Use a small value for remote services, databases, and APIs with rate limits.
- Use a value near the number of useful worker threads for CPU-heavy work.
- Use a larger value for I/O-heavy local work if the underlying storage or
  service benefits from parallelism.
- Keep separate limiters for unrelated resources so one workload does not
  consume another workload's budget.

## Shutdown

Call [method@Dex.Limiter.close] when no new work should start. Pending and
future acquisitions reject with [error@Dex.Error.SEMAPHORE_CLOSED]. Work that
already holds a permit may continue, but releasing after close does not make
new permits available.
