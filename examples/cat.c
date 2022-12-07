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

static DexFuture *
release_buffer (DexFuture *future,
                gpointer user_data)
{
  Buffer *buffer = user_data;
  cat_push_buffer (buffer->cat, buffer);
  return NULL;
}

static DexFuture *
cat_read_cb (DexFuture *future,
             gpointer   user_data)
{
  DexFuture *read_future = dex_future_set_get_future_at (DEX_FUTURE_SET (future), 0);
  const GValue *value = dex_future_get_value (read_future, NULL);
  Cat *cat = user_data;
  Buffer *buffer = g_steal_pointer (&cat->current);

  if (!(buffer->length = g_value_get_int64 (value)))
    return NULL;

  cat->current = cat_pop_buffer (cat);

  return dex_future_all (dex_input_stream_read (cat->input,
                                                cat->current->data,
                                                cat->current->capacity,
                                                G_PRIORITY_DEFAULT),
                         dex_channel_send (cat->channel,
                                           dex_future_new_for_pointer (buffer)),
                         NULL);
}

static DexFuture *
cat_read (Cat *cat)
{
  cat->current = cat_pop_buffer (cat);

  return dex_future_then_loop (dex_future_all (dex_input_stream_read (cat->input,
                                                                      cat->current->data,
                                                                      cat->current->capacity,
                                                                      G_PRIORITY_DEFAULT),
                                               NULL),
                               cat_read_cb, cat, NULL);
}

static DexFuture *
cat_read_routine (gpointer user_data)
{
  Cat *cat = user_data;
  cat->input = G_INPUT_STREAM (g_unix_input_stream_new (cat->read_fd, FALSE));
  return cat_read (cat);
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
cat_write_routine (gpointer user_data)
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
                dex_scheduler_spawn (NULL, cat_read_routine, &cat, NULL),
                dex_scheduler_spawn (NULL, cat_write_routine, &cat, NULL),
                &error))
    {
      g_printerr ("cat: %s\n", error->message);
      g_clear_error (&error);
      ret = EXIT_FAILURE;
    }

  cat_clear (&cat);

  return ret;
}
