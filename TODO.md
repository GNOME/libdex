# TODO

 * Implicit vs explicit support for deferred execution. If we have
   some "await" support, that is when we can kick off work items. If
   language bindings wrap Dex, this is where they can make things
   implicit depending on the language.
 * Generator API to match Fiber semantics w/ yield
 * More/better support for non-standard API using DexAsyncPair
 * Some integration with various I/O API in GLib like GIOChannel
   or other FD based tooling for futures, possibly GPollFD?
 * Support for AIO models other than `io_uring` (using DexAioBackend). This
   is internal only and used by things like DexSemaphore, but it could be made
   more generally available if we find purpose for it.

