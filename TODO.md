# TODO

 * Implicit vs explicit support for deferred execution. If we have
   some "await" support, that is when we can kick off work items. If
   language bindings wrap Dex, this is where they can make things
   implicit depending on the language.
 * ThreadPool schedulers, which should have a GMainContext so that
   we can use them to attach GSource for certain types of futures
   instead of attaching elsewhere.
 * DexCallable needs work so we can bridge to bindings and thunk
   and/or trampoline
 * DexFunction needs to follow above
 * DexTasklet needs API to wire up futures to parameters
 * Scheduling of tasklet on schedulers
 * More/better support for non-standard API using DexAsyncPair
 * Some integration with various I/O API in GLib like GIOChannel
   or other FD based tooling for futures, possibly GPollFD?

