/* dex-aio.c
 *
 * Copyright 2022 Christian Hergert <chergert@redhat.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "dex-aio.h"
#include "dex-aio-backend-private.h"
#include "dex-scheduler-private.h"
#include "dex-thread-storage-private.h"

static DexAioContext *
dex_aio_context_current (void)
{
  DexThreadStorage *storage = dex_thread_storage_get ();

  if (storage->aio_context)
    return storage->aio_context;

  if (storage->scheduler)
    return dex_scheduler_get_aio_context (storage->scheduler);

  g_return_val_if_reached (NULL);
}

/**
 * dex_aio_read:
 * @aio_context: (nullable):
 * @buffer: (array length=count) (element-type guint8) (out caller-allocates):
 * @count: the number of bytes to read
 * @offset: the positioned offset within @fd to read from
 *
 * An asynchronous `pread()` wrapper.
 *
 * Generally you want to provide `NULL` for the @aio_context as that
 * will get the default aio context for your scheduler.
 *
 * Returns: (transfer full): a future that will resolve when the
 *   read completes or rejects with error.
 */
DexFuture *
dex_aio_read (DexAioContext *aio_context,
              int            fd,
              gpointer       buffer,
              gsize          count,
              goffset        offset)
{
  if (aio_context == NULL)
    aio_context = dex_aio_context_current ();

  return dex_aio_backend_read (aio_context->aio_backend, aio_context,
                               fd, buffer, count, offset);
}

/**
 * dex_aio_write:
 * @aio_context: (nullable):
 * @buffer: (array length=count) (element-type guint8):
 * @count: the number of bytes to write from @buffer
 * @offset: the positioned offset within @fd to write at
 *
 * An asynchronous `pwrite()` wrapper.
 *
 * Generally you want to provide `NULL` for the @aio_context as that
 * will get the default aio context for your scheduler.
 *
 * Returns: (transfer full): a future that will resolve when the
 *   write completes or rejects with error.
 */
DexFuture *
dex_aio_write (DexAioContext *aio_context,
               int            fd,
               gconstpointer  buffer,
               gsize          count,
               goffset        offset)
{
  if (aio_context == NULL)
    aio_context = dex_aio_context_current ();

  return dex_aio_backend_write (aio_context->aio_backend, aio_context,
                                fd, buffer, count, offset);
}
