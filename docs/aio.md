Title: Asynchronous IO

The [class@Gio.IOStream] APIs already provide robust support for asynchronous IO.
The common API allows for different types of implementation based on the stream implementation.

Libdex provides wrappers for various APIs.
Coverage is not complete but we do expect additional APIs to be covered in future releases.

# Files, Directories & Streams

## File Management

See [func@Dex.file_copy] for copying files.

See [func@Dex.file_delete] for deleting files.

See [func@Dex.file_move] for moving files.

## File Attributes

See [func@Dex.file_query_info] and [func@Dex.file_query_file_type], and [func@Dex.file_query_exists] for basic querying.

You can set file attributes using [func@Dex.file_set_attributes].

## Directories

You can create a directory or hierarchy of directories using [func@Dex.file_make_directory] and [func@Dex.file_make_directory_with_parents] respectively.

### Enumerating Files

You can create a file enumerator for a directory using [func@Dex.file_enumerate_children].

You can also asynchronously enumerate the files of that directory using [func@Dex.file_enumerator_next_files] which will resolve to a `g_autolist(GFileInfo)` of infos.

## Reading and Writing Files

The [func@Dex.file_read] will provide a [class@Gio.FileInputStream] which can be read from.

A simpler interface to get the bytes of a file is provided via [func@Dex.file_load_contents_bytes].

The [func@Dex.file_replace] will replace a file on disk providing a [class@Gio.FileOutputStream] to write to.
The [func@Dex.file_replace_contents_bytes] provides a simplified API for this when the content is readily available.

## Reading Streams

See [func@Dex.input_stream_read], [func@Dex.input_stream_read_bytes], [func@Dex.input_stream_skip], and [func@Dex.input_stream_close] for working with input streams asynchronously.

## Writing Streams

See [func@Dex.output_stream_write], [func@Dex.output_stream_write_bytes], [func@Dex.output_stream_splice], and [func@Dex.output_stream_close] for writing to streams asynchronously.

# Sockets

The [func@Dex.socket_listener_accept], [func@Dex.socket_client_connect], and [func@Dex.resolver_lookup_by_name] may be helpful when writing socket servers and clients.

# D-Bus

Light integration exists for D-Bus to perform asynchronous method calls.

See [func@Dex.dbus_connection_call], [func@Dex.dbus_connection_call_with_unix_fd_list], [func@Dex.dbus_connection_send_message_with_reply] and [func@Dex.dbus_connection_close].

We expect additional support for D-Bus to come at a later time.

# Subprocesses

You can await completion of a subprocess using [func@Dex.subprocess_wait_check].

# Asynchronous IO with File Descriptors

[class@Gio.IOStream] and related APIs provides much opportunity for streams to be used asynchronously.
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
