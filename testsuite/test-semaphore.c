/* test-semaphore.c
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

#include "dex-semaphore-private.h"

static gboolean
worker_thread_callback (gpointer user_data)
{
  static guint count;

  if (g_atomic_int_add (&count, 1) == 7)
    exit (0);

  return G_SOURCE_CONTINUE;
}

static gpointer
worker_thread_func (gpointer data)
{
  DexSemaphore *semaphore = data;
  GMainContext *main_context = g_main_context_new ();
  GSource *source = dex_semaphore_source_new (0, semaphore, worker_thread_callback, NULL, NULL);

  g_source_attach (source, main_context);
  g_source_unref (source);

  for (;;)
    g_main_context_iteration (main_context, TRUE);

  dex_semaphore_unref (semaphore);

  return NULL;
}

int
main (int argc,
      char *argv[])
{
  DexSemaphore *semaphore = dex_semaphore_new ();
  GThread *threads[8];

  for (guint i = 0; i < G_N_ELEMENTS (threads); i++)
    {
      char *name = g_strdup_printf ("thread-%d", i);
      threads[i] = g_thread_new (name, worker_thread_func, dex_semaphore_ref (semaphore));
      g_free (name);
    }

  dex_semaphore_post_many (semaphore, 8);

  for (guint i = 0; i < G_N_ELEMENTS (threads); i++)
    g_thread_join (threads[i]);

  return 0;
}
