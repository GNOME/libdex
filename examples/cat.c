/*
 * cat.c
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

#include <unistd.h>

#include <gio/gio.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>

#include "cat-util.h"

/* NOTE:
 *
 * `cat` from coreutils is certainly faster than this, especially if you're
 * doing things like `./examples/cat foo > bar` as it will use
 * copy_file_range() to avoid reading into userspace.
 *
 * `gio cat` is likely faster than this doing synchronous IO on the calling
 * thread because it doesn't have to coordinate across thread pools.
 */

static DexFuture *
cat_read_fiber (gpointer user_data)
{
  g_autoptr(GInputStream) stream = NULL;
  Cat *cat = user_data;
  DexFuture *future;
  Buffer *buffer;

  stream = G_INPUT_STREAM (g_unix_input_stream_new (cat->read_fd, FALSE));
  buffer = cat_pop_buffer (cat);
  future = dex_input_stream_read (stream,
                                  buffer->data,
                                  buffer->capacity,
                                  G_PRIORITY_DEFAULT);
  buffer->length = dex_future_await_int64 (future, NULL);
  dex_unref (future);

  if (buffer->length <= 0)
    return NULL;

  for (;;)
    {
      g_autoptr(DexFuture) all = NULL;
      DexFuture *read_future;
      DexFuture *send_future;
      Buffer *next = cat_pop_buffer (cat);

      send_future = dex_channel_send (cat->channel,
                                      dex_future_new_for_pointer (g_steal_pointer (&buffer)));
      read_future = dex_input_stream_read (stream,
                                           next->data,
                                           next->capacity,
                                           G_PRIORITY_DEFAULT);

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
  GOutputStream *stream;

  stream = G_OUTPUT_STREAM (g_unix_output_stream_new (cat->write_fd, FALSE));

  for (;;)
    {
      g_autoptr(DexFuture) recv = dex_channel_receive (cat->channel);
      g_autoptr(DexFuture) wr = NULL;
      Buffer *buffer;
      gssize len;

      if (!(buffer = dex_future_await_pointer (recv, NULL)))
        break;

      wr = dex_output_stream_write (stream,
                                    buffer->data,
                                    buffer->length,
                                    G_PRIORITY_DEFAULT);

      len = dex_future_await_int64 (wr, NULL);
      cat_push_buffer (cat, buffer);

      if (len <= 0)
        break;
    }

  g_clear_object (&stream);

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
                dex_scheduler_spawn_fiber (NULL, cat_read_fiber, &cat, NULL),
                dex_scheduler_spawn_fiber (NULL, cat_write_fiber, &cat, NULL),
                &error))
    {
      g_printerr ("cat: %s\n", error->message);
      g_clear_error (&error);
      ret = EXIT_FAILURE;
    }

  cat_clear (&cat);

  return ret;
}
