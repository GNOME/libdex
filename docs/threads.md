Title: Threads for Blocking APIs

There are often cases where you need to integrate with blocking APIs that do
not fit well into the async or future-based models of the GNOME ecosystem.
In those cases, you may want to use a dedicated thread for blocking calls so
that you do not disrupt main loops, timeouts, or fiber scheduling.

# Creating a Dedicated Thread

Use the [func@Dex.thread_spawn] function to spawn a new thread.
When the thread completes the resulting future will either resolve or reject.

```c
typedef DexFuture *(*DexThreadFunc) (gpointer user_data);

DexFuture *future = dex_thread_spawn ("[my-thread]", thread_func, thread_data,
                                      (GDestroyNotify)thread_data_free);
```

# Waiting for Future Completion

Since dedicated threads do not have a [class@Dex.Scheduler] on them and are not a fiber, you may not await futures.
Awaiting would suspend a fiber stack but there is no such fiber to suspend.

To make integration easier, you may use [func@Dex.thread_wait_for] to wait for a future to complete.
The mechanism used in this case is a mutex and condition variable which will be signaled when the dependent future completes.

# Managing a Fixed Thread Pool

Use [class@Dex.ThreadPool] when you need bounded concurrency for blocking or
foreign work. It is useful when you have a set of operations that can run in
parallel, but you do not want to create an unbounded number of native threads
with [func@Dex.thread_spawn].

```c
DexThreadPool *pool = dex_thread_pool_new (4);

DexFuture *future = dex_thread_pool_submit (pool,
                                            "[thumbnailer]",
                                            run_blocking_job,
                                            job_data,
                                            job_data_free);
```

The pool creates a fixed number of OS threads up front. Each submission is
queued until a worker becomes available. The returned future resolves when the
job completes, or rejects if the job returns an error future.

The optional `thread_name` passed to `dex_thread_pool_submit()` is applied to
the returned future, not to the underlying OS thread. Use it to make the
queued work visible in tracing and debugging output without paying the cost of
renaming the worker itself.

If the pool is shared by multiple workloads and one workload needs a smaller
concurrency budget, submit it through [method@Dex.Limiter.run_on_pool]. That
keeps the pool reusable while limiting how many jobs from that workload may
run at once.

This is a good fit for:

* blocking filesystem or database work;
* wrapping legacy C libraries that do not integrate with GMainContext;
* limiting concurrency when the external dependency is expensive or not
  thread-safe at high fanout.

Important caveats:

* Pool workers do not have a [class@Dex.Scheduler], so you cannot `await()`
  other Dex futures from inside a worker function.
* The pool is for blocking work only. Do not use it for normal async work that
  already integrates with the main loop.
* `dex_thread_pool_close()` is asynchronous. Call it once, keep the returned
  future, and await that future to know shutdown is complete.
* Use [enum@Dex.ThreadPoolShutdownMode.DRAIN] to let queued work finish, or
  [enum@Dex.ThreadPoolShutdownMode.CANCEL_QUEUED] to reject anything still
  waiting in the queue.
* Call `close()` before dropping your last reference if there is any chance the
  pool is still active. That avoids finalization waiting for worker threads in
  an unexpected place.

In practice, the pattern is:

1. Create the pool with the number of concurrent workers you want.
2. Submit each blocking task with `dex_thread_pool_submit()`.
3. Hold onto the returned future if you need the result.
4. Close the pool with `dex_thread_pool_close()`.
5. Wait for the close future before letting the pool go out of scope.
