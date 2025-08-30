Title: Overview

# Overview

Libdex is a [GNOME](https://www.gnome.org/) library that provides support for
futures and fibers in GObject-based applications.

The library attempts to follow well established patterns and terminology around
programming with futures.

Libdex depends on GLib 2.68 or newer.

## Building

To build a program that uses `libdex`, you can use the following command to get
the `CFLAGS` and libraries necessary to compile and link.

```sh
cc hello.c `pkg-config --cflags --libs libdex-1` -o hello
```

Use `#include <libdex.h>` to include support for libdex.

## Platform Support

Libdex, including Fiber support, has been known to work on Linux, FreeBSD,
Solaris, Illumos, macOS, and Windows.

Support for `io_uring` is limited to Linux.

## Futures and Promises

A [class@Dex.Future] is the primary object representing work that will
complete now or at some point in the future. There are various types of
futures, such as a [class@Dex.Promise] or [class@Dex.Timeout].

See the class hierarchy for other types of futures.

## Fibers

Libdex has support for fibers which run on a separate stack from your thread
stack. They are represented with the [class@Dex.Fiber] type which is also a
[class@Dex.Future].

To create a fiber, use [method@Dex.Scheduler.spawn] with the appropriate
scheduler. For thread pool usage, [func@Dex.ThreadPoolScheduler.get_default()]
will get you the default thread pool.

Fibers are scheduled from a [struct@GLib.Source] in a [struct@GLib.MainContext]
which allows them to run cooperatively with other work.

## Awaiting Futures

If you are running on a fiber, you may await completion of a future. This will
suspend your fiber stack and yield back to the main thread until that future
has completed, rejected, or the fiber has been canceled.

Use [method@Dex.Future.await] or it's variants such as
[method@Dex.Future.await_object] to await completion of any future. If you are
not on a fiber, then the await will fail and the `error` argument will be set.

## Reference Counting

Everything that is a [class@Dex.Object], include [class@Dex.Future] may be
referenced using `dex_ref()` and unref'd using `dex_unref()` or `dex_clear()`.

You may also use `g_autoptr(DexFuture)` to automate this.

## Callbacks

When using fibers is not desired you can manually use callbacks based on the
completion or rejection of futures. This is faster than using fibers and does
not require the use of a special stack. However, it can be more cumbersome to
write when in C.

Use [ctor@Dex.Future.then], [ctor@Dex.Future.catch], and
[ctor@Dex.Future.finally] to do typical try/catch/finally semantics. Each
of these return a new [class@Dex.Future] that may be further acted upon.

## Disowning Futures

When a future is no longer needed and unref'd, it will notify the futures it
was depending on that they are no longer requiring their completion. Some
future types may use this to cascade work cancellation.

If this is not a behavior you want (e.g. you want the dependent task to always
complete), then you may use [method@Dex.Future.disown] to disown a future
ensuring it will always complete or reject without cancellation.

## Futures as Sets of Work

A common use of libdex is to await for multiple futures to complete before
taking further action. Use [ctor@Dex.Future.all], [ctor@Dex.Future.all_race],
[ctor@Dex.Future.any], and [ctor@Dex.Future.first] to await multiple futures
with different semantics about when the new future will complete.

## Work Queues

In addition to fibers, [class@Dex.Scheduler] can also schedule general work
items to be run. Use [method@Dex.Scheduler.push] to push a work item onto that
scheduler.

On systems that support double-wide compare-and-swap, this will avoid an
allocation for the work item and data pair. They will be placed into the
wait-free work queue atomically.

## Thread Pools and Schedulers

In many thread pool implementations, the worker thread is only executing
work items. In libdex, thread pools can both service short work items and
fibers along with longer running timeouts and generalized `GSource` on the
`GMainContext`. This allows a great deal of flexibility in using futures on
the thread pool.

The thread pool is implemented as a global work queue and a series of lock-free
work queues per worker thread. Work stealing is also used between thread pools
for general purpose work items. However, fibers will never be migrated between
threads in order to ensure runtime safety.

Because of the expectation that you will await futures on the thread pool
threads, should you need to do long blocking work you may consider using
[func@Dex.thread_spawn]. This thread may not await like fibers as it is a
real operating system thread. You may however block on a future using
[func@Dex.thread_wait_for].
