# TODO

 * Lots of course
 * Scheduler needs API fleshed out
 * Main/ThreadPool schedulers once above is done
 * DexCallable needs work so we can bridge to bindings and thunk
   and/or trampoline
 * DexFunction needs to follow above
 * DexTasklet needs API to wire up futures to parameters
 * Scheduling of tasklet on schedulers
 * More/better support for non-standard API using DexAsyncPair
 * Some integration with various I/O API in GLib like GIOChannel
   or other FD based tooling for futures, possibly GPollFD?
 * Futures to handle signals using GUnixSignalSource

