# Dex

Dex is a library supporting "(D)eferred (Ex)ecution" with the explicit goal
of integrating with GNOME and GTK-based applications.

It provides primatives for supporting futures in a variety of ways with both
read-only and writable views. Additionally, integration with existing
asynchronous-based APIs is provided through the use of wrapper promises.

Dex is licensed as LGPL-2.1+.

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

## Types

 * DexObject (Abstract)
   * DexFuture (Abstract)
     * DexAsyncPair (Final)
     * DexBlock (Final)
     * DexCancellable (Final)
     * DexPromise (Final)
     * DexTasklet (Final)
     * DexTimeout (Final)
   * DexScheduler (Abstract)
     * DexMainScheduler (Final)
     * DexThreadPoolScheduler (Final)
   * DexCallbale (Abstract)
     * DexFunction (Final)

