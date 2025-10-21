Title: Debugging

If you experience a crash with Libdex, usually it is a programming error in your application.
Here are a few common things to look for.

# Improper Ownership Transfer

Many if not most APIs in Libdex will transfer ownership as part of the constructor or methods.
That typically plays nicely with chaining futures together when writing code in C.

Be aware that if you are doing something with a future like [method@Dex.Future.await] that ownership of the future is transferred to the method.
The same goes for methods like [ctor@Dex.Future.all].

The `dex_future_allv()` and similar array-of-future functions do not transfer full because they are more likely to be used by language bindings.

# Performance

If libdex was built with `-Dsysprof=true` then Libdex may emit marks representing the lifetimes of [class@Dex.Future].
You can run `sysprof-cli capture.syscap -- ./program` to record your application.
Open the capture with the Sysprof application to view future lifetimes in the Marks section.

# Stack Overflow on Fibers

The default fiber size is rather small since we are trying to make it convenient for applications to use a large number of fibers.
Some work done on fibers may expect to have larger stack space.
If you are doing work that is expected to call into graphics drivers (OpenGL, Vulkan), image codecs (Rsvg, JXL), or multimedia codecs (GStreamer) you may want to use a larger fiber size than the default (currently 128 kB).
