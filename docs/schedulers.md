Title: Schedulers & FIbers

# Schedulers

The [class@Dex.Scheduler] is responsible for running work items on a thread.
This is performed by integrating with the threads [struct@GLib.MainContext].

The main thread of your application will have a [class@Dex.MainScheduler] as the assigned scheduler.

The scheduler manages callbacks such as work items created with [method@Dex.Scheduler.push].

You can get the default scheduler for the application's main thread using [func@Dex.Scheduler.get_default].

The thread's scheduler can be retrieved with [func@Dex.Scheduler.ref_thread_default].

# Thread Pool Scheduling

Libdex manages a thread pool which may be retrieved using [func@Dex.ThreadPoolScheduler.get_default].

The thread pool scheduler will manage a number of threads that is deemed useful based on the number of CPU available.

Work items created from outside of the thread pool are placed into a global queue.
Thread pool workers will take items from the global queue when they have no more items to process.

To avoid "thundering herd" situations often caused by global queues and thread pools a pollable semaphore is used.
On Linux, specifically, io_uring and eventfd combined with `EFD_SEMAPHORE` allow waking up a single worker when a work item is queued.

All thread pool workers have a local [class@Dex.Scheduler] so use of timeouts and other [struct@GLib.Source] features continue to work.

If you need to interact with long-blocking API calls it is better to use [func@Dex.thread_spawn] rather than a thread pool thread.

Thread pool workers use a work-stealing wait-free queue which allows the worker to push work items onto one side of the queue quickly.
Doing so also helps improve cacheline effectiveness.

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

## Cancellation

Fibers may be cancelled if the fiber has been discarded by all futures awaiting completion.
Fibers will always exit through a natural exit point such as a pending "await".
All attempts to await will reject with error once a fiber has been cancelled.

If you want to ignore cancellation of fibers, use [method@Dex.Future.disown] on the fiber after creation.
