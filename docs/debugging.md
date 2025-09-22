Title: Debugging

# Ownership Transfer

Many if not most APIs in Libdex will transfer ownership as part of the constructor or methods.
That typically plays nicely with chaining futures together when writing code in C.

Be aware that if you are doing something with a future like [method@Dex.Future.await] that ownership of the future is transferred to the method.
The same goes for methods like [ctor@Dex.Future.all].

# Performance

If libdex was built with `-Dsysprof=true` then Libdex may emit marks representing the lifetimes of [class@Dex.Future].
You can run `sysprof-cli capture.syscap -- ./program` to record your application.
Open the capture with the Sysprof application to view future lifetimes in the Marks section.
