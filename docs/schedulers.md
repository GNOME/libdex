Title: Schedulers & Fibers

# Schedulers

The [class@Dex.Scheduler] is responsible for running work items on a thread.
This is performed by integrating with the threads [struct@GLib.MainContext].

The main thread of your application will have a [class@Dex.MainScheduler] as the assigned scheduler.

The scheduler manages callbacks such as work items created with [method@Dex.Scheduler.push].

You can get the default scheduler for the application's main thread using [func@Dex.Scheduler.get_default].

The thread's scheduler can be retrieved with [func@Dex.Scheduler.ref_thread_default].

# Thread Pool Scheduling

Libdex manages a thread pool which may be retrieved using [func@Dex.ThreadPoolScheduler.get_default].

The thread pool scheduler sizes its workers from the CPUs available to the
creating thread. On Linux, it counts distinct physical cores in that thread's
affinity mask. It creates one fewer worker than the core count when possible to
leave capacity for the main thread, and caps the pool at 32 workers. If CPU
topology is unavailable, it uses the number of available logical CPUs instead.

Workers inherit the creating thread's affinity mask. The shared default pool is
created on its first use, so an affinity restriction on that first caller also
applies to its workers when they start. Create it before applying a temporary
thread affinity restriction if it should use a wider mask.

Work items created from outside of the thread pool are placed into a global queue.
Thread pool workers will take items from the global queue when they have no more items to process.

All thread pool workers have a local [class@Dex.Scheduler] so use of timeouts and other [struct@GLib.Source] features continue to work.

If you need to interact with long-blocking API calls it is better to use [func@Dex.thread_spawn] rather than a thread pool thread.

Thread pool workers use a work-stealing wait-free queue which allows the worker to push work items onto one side of the queue quickly.
Doing so also helps improve cacheline effectiveness.

Workers sleep in their main context when they have no work. When a worker
publishes enough local work for another worker to help, it sends a directed
request to one peer. Requests to a peer are coalesced, and a successful steal
may wake one additional peer while useful backlog remains. This allows the pool
to increase parallelism gradually without waking every worker for one item.
An already-busy peer may retain a request until it drains its own local work,
and a thief continues while any victim observed later in its scan has backlog.

# Fibers

Fibers are a type of stackful co-routine.
A new stack is created and a trampoline is performed onto the stack from the current thread.

Use [method@Scheduler.spawn] to create a new fiber.

When a fiber calls a [method@Dex.Future.await], similar function, or returns the fiber is suspended and execution returns to the scheduler.

By default, fibers have a 128-kb stack with a guard page at the end.
Fiber stacks are pooled so that they may be reused during heavy use.

Fibers are a [class@Dex.Future] which means you can await the completion of a fiber just like any other future.

Note that fibers are pinned to a scheduler.
They will not be migrated between schedulers even when a thread pool is in use.

# Stackless Coroutines

Coroutines are a stackless alternative that are still futures managed by the same
[class@Dex.Scheduler]. Use [method@Dex.Scheduler.spawn_coroutine] to create them.

A coroutine function is given a [struct@Dex.CoroutineContext] and is expected to
return a [class@Dex.Future] or suspend by returning `NULL`.

Stackless coroutines generally have lower memory overhead than fibers when you are
mainly waiting on futures between steps.

Use `DEX_COROUTINE_BEGIN` and `DEX_COROUTINE_END` when writing
coroutines. Suspend with one of the `DEX_COROUTINE_SUSPEND_*` helpers.
See [Coroutines](coroutines.html) for more guidance.

## Cancellation

Fibers may be cancelled if the fiber has been discarded by all futures awaiting completion.
Fibers will always exit through a natural exit point such as a pending "await".
All attempts to await will reject with error once a fiber has been cancelled.

If you want to ignore cancellation of fibers, use [method@Dex.Future.disown] on the fiber after creation.
