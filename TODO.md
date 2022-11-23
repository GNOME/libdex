# TODO

 * Implicit vs explicit support for deferred execution. If we have
   some "await" support, that is when we can kick off work items. If
   language bindings wrap Dex, this is where they can make things
   implicit depending on the language.
 * DexCallable needs work so we can bridge to bindings and thunk
   and/or trampoline
 * DexFunction needs to follow above
 * DexTasklet needs API to wire up futures to parameters, this is
   where we may have thunks to transform promise values into
   parameters for the callable.
 * Scheduling of tasklet on schedulers
 * More/better support for non-standard API using DexAsyncPair
 * Some integration with various I/O API in GLib like GIOChannel
   or other FD based tooling for futures, possibly GPollFD?
 * Support for AIO models other than `io_uring` (using DexAioBackend)
 * DexThreadPoolWorker should also be a DexScheduler, so that it can
   be the default for the thread. That way tasks come back to the
   same thread they are executing on. This would provide some amount
   of thread pinning for free.

