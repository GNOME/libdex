Title: Async IO on File-Descriptors

[class@Gio.IOStream] and related APIs provides much opertunity for streams to be used asynchronously.
There may be cases where you want similar behavior with traditional file-descriptors.

Libdex provides a set of AIO-like functions for traditional file-descriptors which may be backed with more efficient mechanisms.

[class@Gio.IOStream] typically uses a thread pool of blocking IO operations on Linux and other operating systems because that was the fastest method when the APIs were created.
However, on some operating systems such as Linux, faster methods finally exist.

On Linux, `io_uring` can be used for asynchronous IO and is provided in the form of a [class@Dex.Future].

# Asynchronous Reads

```c
DexFuture *dex_aio_read  (DexAioContext *aio_context,
                          int            fd,
                          gpointer       buffer,
                          gsize          count,
                          goffset        offset)
```

Use the `dex_aio_read()` function to read from a file-descriptor.
The result will be a future that resolves to a `gint64` containing the number of bytes read.

If there was a failure, the future will reject using the appropriate error code.

Your `buffer` must stay alive for the duration of the asynchronous read.
One easy way to make that happen is to wrap the resulting future in a `dex_future_then()` which stores the buffer as `user_data` and releases it when finished.

If you are doing buffer pooling, more effort may be required.

# Asynchronous Writes

```c
DexFuture *dex_aio_write (DexAioContext *aio_context,
                          int            fd,
                          gconstpointer  buffer,
                          gsize          count,
                          goffset        offset)
```

A similar API exists as `dex_aio_read()` but for writing.
It too will resolve to a `gint64` containing the number of bytes written.

`buffer` must be kept alive for the duration of the call and it is the callers responsibility to do so.
