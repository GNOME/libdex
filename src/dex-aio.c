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

DexFuture *
dex_aio_read (DexAioContext *aio_context,
              int            fd,
              gpointer       buffer,
              gsize          count,
              goffset        offset)
{
  return dex_aio_backend_read (aio_context->aio_backend, aio_context,
                               fd, buffer, count, offset);
}

DexFuture *
dex_aio_write (DexAioContext *aio_context,
               int            fd,
               gconstpointer  buffer,
               gsize          count,
               goffset        offset)
{
  return dex_aio_backend_write (aio_context->aio_backend, aio_context,
                                fd, buffer, count, offset);
}
