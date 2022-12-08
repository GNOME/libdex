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
release_buffer (DexFuture *future,
                gpointer user_data)
{
  Buffer *buffer = user_data;
  cat_push_buffer (buffer->cat, buffer);
  return NULL;
}

static DexFuture *
cat_read_fiber (gpointer user_data)
{
  Cat *cat = user_data;
  GInputStream *stream;
  const GValue *value;
  DexFuture *future;
  Buffer *buffer;

  stream = G_INPUT_STREAM (g_unix_input_stream_new (cat->read_fd, FALSE));
  buffer = cat_pop_buffer (cat);
  future = dex_input_stream_read (stream,
                                  buffer->data,
                                  buffer->capacity,
                                  G_PRIORITY_DEFAULT);
  if (!(value = dex_future_await (future, NULL)))
    return NULL;
  buffer->length = g_value_get_int64 (value);
  dex_unref (future);

  for (;;)
    {
      DexFuture *read_future;
      Buffer *next = cat_pop_buffer (cat);

      future = dex_future_all (dex_input_stream_read (stream,
                                                      next->data,
                                                      next->capacity,
                                                      G_PRIORITY_DEFAULT),
                               dex_channel_send (cat->channel,
                                                 dex_future_new_for_pointer (g_steal_pointer (&buffer))),
                               NULL);

      dex_future_await (future, NULL);

      read_future = dex_future_set_get_future_at (DEX_FUTURE_SET (future), 0);
      value = dex_future_get_value (read_future, NULL);

      if (value == NULL || !(next->length = g_value_get_int64 (value)))
        {
          dex_channel_close_send (cat->channel);
          cat_push_buffer (cat, next);
          dex_unref (future);
          break;
        }

      buffer = next;
      dex_unref (future);
    }

  g_clear_object (&stream);

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
cat_write_cb (DexFuture *completed,
              gpointer   user_data)
{
  Cat *cat = user_data;
  DexFuture *buffer_future = dex_future_set_get_future_at (DEX_FUTURE_SET (completed), 0);
  const GValue *value = dex_future_get_value (buffer_future, NULL);
  Buffer *buffer;

  if (value == NULL)
    return NULL;

  buffer = g_value_get_pointer (value);
  return dex_future_all (dex_channel_receive (cat->channel),
                         dex_future_finally (dex_output_stream_write (cat->output,
                                                                      buffer->data,
                                                                      buffer->length,
                                                                      G_PRIORITY_DEFAULT),
                                             release_buffer, buffer, NULL),
                         NULL);
}

static DexFuture *
cat_write (Cat *cat)
{
  DexFuture *future;

  future = dex_channel_receive (cat->channel);
  future = dex_future_all (future, NULL);
  future = dex_future_finally_loop (future, cat_write_cb, cat, NULL);

  return future;
}

static DexFuture *
cat_write_fiber (gpointer user_data)
{
  Cat *cat = user_data;
  cat->output = G_OUTPUT_STREAM (g_unix_output_stream_new (cat->write_fd, FALSE));
  return cat_write (cat);
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
