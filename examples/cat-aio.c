/*
 * cat-aio.c
 *
 * Copyright 2022 Christian Hergert <chergert@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* A variant of cat.c which uses the internal/private AIO for testing
 * performance compared to GIO async/finish pairs.
 */

#include <unistd.h>

#include <gio/gio.h>

#include <libdex.h>

#include "dex-aio-private.h"

#include "cat-util.h"

static DexFuture *
cat_read_fiber (gpointer user_data)
{
  Cat *cat = user_data;
  Buffer *buffer = NULL;

  for (;;)
    {
      g_autoptr(DexFuture) all = NULL;
      DexFuture *send_future = NULL;
      DexFuture *read_future;
      Buffer *next = cat_pop_buffer (cat);

      if (buffer != NULL)
        send_future = dex_channel_send (cat->channel,
                                        dex_future_new_for_pointer (g_steal_pointer (&buffer)));

      read_future = dex_aio_read (NULL,
                                  cat->read_fd,
                                  next->data,
                                  next->capacity,
                                  -1);

      all = dex_future_all (read_future, send_future, NULL);
      dex_future_await (all, NULL);

      next->length = dex_future_await_int64 (read_future, NULL);

      if (next->length <= 0)
        {
          dex_channel_close_send (cat->channel);
          cat_push_buffer (cat, next);
          break;
        }

      buffer = next;
    }

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
cat_write_fiber (gpointer user_data)
{
  Cat *cat = user_data;

  for (;;)
    {
      g_autoptr(DexFuture) recv = dex_channel_receive (cat->channel);
      g_autoptr(DexFuture) wr = NULL;
      Buffer *buffer;
      gssize len;

      if (!(buffer = dex_future_await_pointer (recv, NULL)))
        break;

      wr = dex_aio_write (NULL,
                          cat->write_fd,
                          buffer->data,
                          buffer->length,
                          -1);

      len = dex_future_await_int64 (wr, NULL);
      cat_push_buffer (cat, buffer);

      if (len <= 0)
        break;
    }

  return dex_future_new_for_boolean (TRUE);
}

int
main (int   argc,
      char *argv[])
{
  GError *error = NULL;
  Cat cat;
  int ret;

  dex_init ();

  if (!cat_init (&cat, &argc, &argv, &error) ||
      !cat_run (&cat,
                dex_scheduler_spawn (NULL, cat_read_fiber, &cat, NULL),
                dex_scheduler_spawn (NULL, cat_write_fiber, &cat, NULL),
                &error))
    {
      g_printerr ("cat: %s\n", error->message);
      g_clear_error (&error);
      ret = EXIT_FAILURE;
    }

  cat_clear (&cat);

  return ret;
}
