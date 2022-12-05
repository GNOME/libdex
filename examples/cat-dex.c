/*
 * cat-dex.c
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

/* This is like cat.c but uses Dex for multiplexing IO instead of
 * relying on async GIO operations, which are often on an IO thread
 * and require significant synchronization.
 */

#include "config.h"

#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include <gio/gio.h>

#include <libdex.h>

#include "dex-aio-private.h"

#define BUFSIZE 4096*4

static GFile         *file;
static int            read_fd;
static int            write_fd = STDOUT_FILENO;
static DexChannel    *channel;
static GMainLoop     *main_loop;
static gsize          size;
static GBytes        *read_bytes;
static GBytes        *write_bytes;
static GQueue         pool;

static DexFuture *
read_next (void)
{
  GBytes *bytes = g_queue_pop_head (&pool);

  if (bytes == NULL)
    {
      gpointer buf = g_aligned_alloc (BUFSIZE, 1, 4096);
      bytes = g_bytes_new_with_free_func (buf, BUFSIZE, g_aligned_free, buf);
    }

  read_bytes = bytes;

  return dex_aio_read (NULL,
                       read_fd,
                       (gpointer)g_bytes_get_data (bytes, NULL),
                       g_bytes_get_size (bytes),
                       -1);
}

static DexFuture *
read_loop (DexFuture *future,
           gpointer   user_data)
{
  DexFuture *read_future = dex_future_set_get_future_at (DEX_FUTURE_SET (future), 0);
  const GValue *value = dex_future_get_value (read_future, NULL);
  gsize len = g_value_get_int64 (value);

  if (len <= 0)
    {
      dex_channel_close_send (channel);
      return NULL;
    }

  if (len != g_bytes_get_size (read_bytes))
    {
      GBytes *bytes = g_bytes_new_from_bytes (read_bytes, 0, len);
      g_bytes_unref (read_bytes);
      read_bytes = bytes;
    }

  size -= len;

  return dex_future_all (read_next (),
                         dex_channel_send (channel,
                                           DEX_FUTURE (dex_promise_new_take_boxed (G_TYPE_BYTES,
                                                                                   g_steal_pointer (&read_bytes)))),
                         NULL);
}

static DexFuture *
read_routine (gpointer user_data)
{
  return dex_future_then_loop (dex_future_all (read_next (), NULL),
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

  if (write_bytes != NULL && g_bytes_get_size (write_bytes) == BUFSIZE)
    g_queue_push_tail (&pool, g_steal_pointer (&write_bytes));
  write_bytes = g_value_dup_boxed (value);

  return dex_future_all (dex_channel_receive (channel),
                         dex_aio_write (NULL,
                                        write_fd,
                                        g_bytes_get_data (write_bytes, NULL),
                                        g_bytes_get_size (write_bytes),
                                        -1),
                         NULL);
}

static DexFuture *
write_routine (gpointer user_data)
{
  return dex_future_then_loop (dex_future_all (dex_channel_receive (channel), NULL),
                               write_loop, NULL, NULL);
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

  if (-1 == (read_fd = open (argv[1], O_RDONLY)))
    {
      int errsv = errno;
      g_printerr ("Failed to open %s: %s\n",
                  argv[1], g_strerror (errsv));
      return EXIT_FAILURE;
    }

  size = g_file_info_get_size (info);
  channel = dex_channel_new (32);

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
  g_clear_pointer (&write_bytes, g_bytes_unref);
  g_clear_pointer (&read_bytes, g_bytes_unref);

  close (read_fd);

  return EXIT_SUCCESS;
}
