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

#include <libdex.h>

#include "dex-aio-backend-private.h"
#include "dex-semaphore-private.h"
#include "dex-thread-storage-private.h"

#define N_THREADS 32

static guint total_count;
static gboolean shutdown;
static guint done;

typedef struct
{
  DexSemaphore *semaphore;
  GThread *thread;
  GSource *source;
  int threadid;
  int handled;
} WorkerState;

static DexFuture *
worker_thread_callback (DexFuture *future,
                        gpointer   user_data)
{
  WorkerState *state = user_data;

  g_atomic_int_inc (&total_count);
  state->handled++;

  return dex_semaphore_wait (state->semaphore);
}

static gpointer
worker_thread_func (gpointer data)
{
  WorkerState *state = data;
  DexAioBackend *aio_backend = dex_aio_backend_get_default ();
  DexAioContext *aio_context = dex_aio_backend_create_context (aio_backend);
  GMainContext *main_context = g_main_context_new ();
  char *name = g_strdup_printf ("semaphore-thread-%u", state->threadid);
  DexFuture *future;

  state->source = (GSource *)aio_context;

  dex_thread_storage_get ()->aio_context = aio_context;

  g_source_set_name (state->source, name);
  g_source_attach (state->source, main_context);

  future = dex_semaphore_wait (state->semaphore);
  future = dex_future_then_loop (future, worker_thread_callback, state, NULL);

  while (!g_atomic_int_get (&shutdown))
    g_main_context_iteration (main_context, TRUE);

  dex_unref (future);
  g_free (name);

  g_atomic_int_inc (&done);

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
      state[i].thread = g_thread_new (name, worker_thread_func, &state[i]);

      g_free (name);
    }

  g_usleep (G_USEC_PER_SEC/2);

  for (guint i = 0; i < 10; i++)
    {
      int count = 10000;
      g_atomic_int_set (&total_count, 0);
      dex_semaphore_post_many (semaphore, count);
      g_usleep (G_USEC_PER_SEC);
      g_print ("Expected %u, got %u\n", count, g_atomic_int_get (&total_count));
      g_assert_cmpint (g_atomic_int_get (&total_count), ==, count);
    }

  g_atomic_int_set (&shutdown, TRUE);

  while (g_atomic_int_get (&done) < N_THREADS)
    dex_semaphore_post (semaphore);

  for (guint i = 0; i < G_N_ELEMENTS (state); i++)
    {
      g_thread_join (state[i].thread);

      g_print ("Thread %d handled %d items\n", i, state[i].handled);
    }

  return 0;
}
