Title: Threads for Blocking APIs

There are often needs to integrate with blocking APIs that do not fit well into the async or future-based models of the GNOME ecosystem.
In those cases, you may want to use a dedicated thread for blocking calls so that you do not disrupt main loops, timeouts, or fiber scheduling.

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
