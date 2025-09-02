Title: Channels

# Overview

[class@Dex.Channel] provides a producer consumer queue for applications.
The channel has a read and a write side that allows you to put futures into the queue.
Futures are used because they provide a convenient container that is used elsewhere in the library.

A common use of channels is to dispatch work to a worker fiber.

You can close the read or the write side independently which will cause the other side to be notified if waiting on a future.

Use [method@Dex.Channel.close_receive] to close the read side of the channel.
You should do this when stopping a worker fiber so that producers are notified that submitting items into the channel has failed.

Use [method@Dex.Channel.close_send] to close the write side of the channel.
This allows consumers to receive notification through the form of a rejection that reading from the channel will no longer succeed.
