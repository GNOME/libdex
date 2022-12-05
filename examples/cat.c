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
#include <gio/gunixoutputstream.h>

#include <libdex.h>

static GFile         *file;
static GInputStream  *input;
static GOutputStream *output;
static DexChannel    *channel;
static GMainLoop     *main_loop;
static gsize          size;

#define KB(n) ((n)*1024L)

static DexFuture *
read_loop (DexFuture *future,
           gpointer   user_data)
{
  const GValue *value;

  if (DEX_IS_FUTURE_SET (future))
    future = dex_future_set_get_future_at (DEX_FUTURE_SET (future), 0);

  value = dex_future_get_value (future, NULL);
  if (G_VALUE_HOLDS (value, G_TYPE_BYTES))
    {
      GBytes *bytes = g_value_get_boxed (value);
      if (g_bytes_get_size (bytes) > 0)
        {
          size -= g_bytes_get_size (bytes);
          return dex_channel_send (channel, dex_ref (future));
        }
    }

  dex_channel_close_send (channel);

  return NULL;
}

static DexFuture *
read_routine (gpointer user_data)
{
  return dex_future_then_loop (dex_input_stream_read_bytes (input, size, 0),
                               read_loop, NULL, NULL);
}

static DexFuture *
write_loop (DexFuture *future,
            gpointer   user_data)
{
  DexFuture *bytes_future = dex_future_set_get_future_at (DEX_FUTURE_SET (future), 0);
  const GValue *value = dex_future_get_value (bytes_future, NULL);

  if (value == NULL)
    return NULL;

  return dex_future_all (dex_channel_receive (channel),
                         dex_output_stream_write_bytes (output, g_value_get_boxed (value), 0),
                         NULL);
}

static DexFuture *
write_routine (gpointer user_data)
{
  DexFuture *future;

  future = dex_channel_receive (channel);
  future = dex_future_all (future, NULL);
  future = dex_future_then_loop (future, write_loop, NULL, NULL);

  return future;
}

static DexFuture *
cat_finished (DexFuture *future,
              gpointer   user_data)
{
  g_main_loop_quit (main_loop);
  return NULL;
}

int
main (int   argc,
      char *argv[])
{
  const GValue *value;
  DexFuture *future;
  GError *error = NULL;
  GFileInfo *info = NULL;

  dex_init ();

  if (argc != 2)
    {
      g_printerr ("usage: %s FILE\n", argv[0]);
      return EXIT_FAILURE;
    }

  main_loop = g_main_loop_new (NULL, FALSE);
  file = g_file_new_for_commandline_arg (argv[1]);

  if (!(info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_SIZE, 0, NULL, &error)))
    {
      g_printerr ("Failed to open %s: %s\n", argv[1], error->message);
      return EXIT_FAILURE;
    }

  size = g_file_info_get_size (info);
  output = g_unix_output_stream_new (STDOUT_FILENO, FALSE);
  channel = dex_channel_new (4);

  if (!(input = G_INPUT_STREAM (g_file_read (file, NULL, &error))))
    {
      g_printerr ("Failed to open %s: %s\n", argv[1], error->message);
      return EXIT_FAILURE;
    }

  /* Spawn routines for read and write loops, communicate with channel */
  future = dex_future_all (dex_scheduler_spawn (NULL, read_routine, NULL, NULL),
                           dex_scheduler_spawn (NULL, write_routine, NULL, NULL),
                           NULL);
  future = dex_future_finally (future, cat_finished, NULL, NULL);

  g_main_loop_run (main_loop);

  if (!(value = dex_future_get_value (future, &error)))
    {
      g_printerr ("Failed to cat %s: %s\n", argv[1], error->message);
      return EXIT_FAILURE;
    }

  dex_clear (&future);
  g_clear_object (&file);
  g_clear_object (&input);
  g_clear_object (&output);

  return EXIT_SUCCESS;
}
