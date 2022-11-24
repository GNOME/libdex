# Dex

**This library is unfinished and still being created, Don't use it**

Dex is a library supporting "(D)eferred (Ex)ecution" with the explicit goal
of integrating with GNOME and GTK-based applications.

It provides primatives for supporting futures in a variety of ways with both
read-only and writable views. Additionally, integration with existing
asynchronous-based APIs is provided through the use of wrapper promises.

Dex is licensed as LGPL-2.1+.

## More Information

You can read about why this is being created and what led to it over the
past two decades of contributing to GNOME and GTK.

https://blogs.gnome.org/chergert/2022/11/24/concurrency-parallelism-i-o-scheduling-thread-pooling-and-work-stealing/

## Implementation Notes

While Dex is using GObject and GIO, it implements it's own fundamental type
(DexObject) for which all other types inherit. Given the concurrent and
parallel nature of futures and the many situations to support, it is the
authors opinion that the performance drawbacks of such a flexible type as
GObject is too costly. By controlling the feature-set to strictly what is
necessary we can avoid much of the slow paths in GObject.

You wont notice much of a difference though, as types are generally defined and
used very similarly to GObject's but with different macro names.

You can see this elsewhere in both GStreamer and GTK 4's render nodes.

## Terminology

 * **Future** describes something that can either resolve (succeed) or
   reject (fail) now or in the future. It's resolved/rejected value is
   immutable after completing.
 * **Resolved** indicates that a future has completed successfully and
   provided a value which can be read by the consumer.
 * **Rejected** indicates that a future has completed with failure and
   provided an error which can be read by the consumer.
 * **Promise** is a **Future** that allows user code to set the resolved
   or rejected value once.
 * **Tasklet** is a **Future** that allows specifying a function to be
   called once future parameters have been resolved. It is run by a
   scheduler and eventually resolves or rejects based on the attached
   callable.
 * **Callable** is an abstraction on a C function/closure or some other
   trampolined code that can be invoked and provided arguments along with
   a return value.
 * **Parameter** contain type information and position to a **Callable**
 * **Arguments** are the values for **Parameters**

## Types

 * DexObject (Abstract)
   * DexFuture (Abstract)
     * DexAsyncPair (Final)
     * DexBlock (Final)
     * DexCancellable (Final)
     * DexFutureSet (Final)
     * DexPromise (Final)
     * DexTasklet (Final)
     * DexTimeout (Final)
     * DexUnixSignal (Final)
   * DexScheduler (Abstract)
     * DexMainScheduler (Final)
     * DexThreadPoolScheduler (Final)
     * DexThreadPoolWorker (Final)
   * DexCallbale (Abstract)
     * DexFunction (Final)
 * GSource
   * DexAioContext

## Internal Types

  * DexWorkQueue
  * DexWorkStealingQueue
  * DexSemaphore
  * DexAioBackend
    * DexUringAioBackend

