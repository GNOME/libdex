from gi.repository import Dex, GLib
import asyncio

_orig_future_init = Dex.Future.__init__

def _future__await__(self):
    loop = asyncio.get_event_loop()
    if loop is None:
        raise RuntimeError("No asyncio event loop running")

    async_fut = loop.create_future()

    def on_done(dex_fut, user_data=None):
        try:
            value = dex_fut.get_value()
        except Exception as e:
            loop.call_soon_threadsafe(async_fut.set_exception, e)
        else:
            loop.call_soon_threadsafe(async_fut.set_result, value)
        finally:
            async_fut.dex_future = None

    async_fut.dex_future = Dex.Future.finally_(self, on_done)

    return (yield from async_fut.__await__())

Dex.Future.__await__ = _future__await__

def _future_to_asyncio(self):
    loop = asyncio.get_event_loop()
    if loop is None:
        raise RuntimeError("No asyncio event loop")
    return self

Dex.Future.to_asyncio = _future_to_asyncio
