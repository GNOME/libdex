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

#define N_THREADS 32

typedef struct
{
  DexSemaphore *semaphore;
  GThread *thread;
  int threadid;
  int count;
} WorkerState;

static gboolean
worker_thread_callback (gpointer user_data)
{
  WorkerState *state = user_data;

  state->count++;

  return G_SOURCE_CONTINUE;
}

static gpointer
worker_thread_func (gpointer data)
{
  WorkerState *state = data;
  GMainContext *main_context = g_main_context_new ();
  GSource *source = dex_semaphore_source_new (0, state->semaphore, worker_thread_callback, state, NULL);
  char *name = g_strdup_printf ("semaphore-thread-%u", state->threadid);

  g_source_set_name (source, name);
  g_source_attach (source, main_context);
  g_source_unref (source);
  g_free (name);

  for (;;)
    g_main_context_iteration (main_context, TRUE);

  return NULL;
}

int
main (int argc,
      char *argv[])
{
  DexSemaphore *semaphore = dex_semaphore_new ();
  WorkerState state[N_THREADS] = {{0}};

  for (guint i = 0; i < G_N_ELEMENTS (state); i++)
    {
      char *name = g_strdup_printf ("test-semaphore-%u", i);

      state[i].semaphore = semaphore;
      state[i].threadid = i;
      state[i].count = 0;
      state[i].thread = g_thread_new (name, worker_thread_func, &state[i]);

      g_free (name);
    }

  g_usleep (G_USEC_PER_SEC/2);

  for (guint i = 0; i < 10; i++)
    {
      dex_semaphore_post_many (semaphore, 3);
      g_usleep (G_USEC_PER_SEC);
      g_print ("==============\n");
    }

#if 0
  for (guint i = 0; i < G_N_ELEMENTS (state); i++)
    g_thread_join (state[i].thread);
#endif

  return 0;
}
