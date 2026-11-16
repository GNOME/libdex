/* generators.c
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <libdex.h>

typedef struct
{
  DexDeferred *job;
  int          calls;
} Producer;

static DexFuture *
run_job (gpointer data)
{
  Producer *state = data;

  state->calls++;
  return dex_future_new_true ();
}

static DexFuture *
fiber_jobs (gpointer data)
{
  Producer *state = data;
  GError *error = NULL;

  if (!dex_fiber_yield_future (DEX_FUTURE (state->job), &error))
    return dex_future_new_for_error (error);
  return dex_future_new_true ();
}

static DexFuture *
coroutine_jobs (DexCoroutineContext *context,
                gpointer             data)
{
  Producer *state = data;
  GError *error = NULL;

  DEX_COROUTINE_BEGIN (context);
  DEX_COROUTINE_YIELD_FUTURE (DEX_FUTURE (state->job), &error);
  if (error != NULL)
    return dex_future_new_for_error (error);
  return dex_future_new_true ();
  DEX_COROUTINE_END;
}

static DexFuture *
consume (gpointer data)
{
  for (guint kind = 0; kind < 2; kind++)
    {
      Producer state = { 0 };
      DexGenerator *generator;
      DexFuture *item = NULL;

      state.job = dex_deferred_new (NULL, run_job, &state, NULL);
      if (kind)
        generator = dex_generator_new_coroutine (NULL, coroutine_jobs, &state, NULL);
      else
        generator = dex_generator_new_fiber (NULL, 0, fiber_jobs, &state, NULL);
      g_assert_true (dex_generator_await_next_future (generator, &item, NULL));
      g_assert_true (item == DEX_FUTURE (state.job));
      g_assert_cmpint (state.calls, ==, 0);
      dex_unref (item);
      g_assert_true (dex_await_boolean (dex_generator_close_after_drain (generator), NULL));
      dex_unref (generator);
      dex_unref (state.job);
      g_assert_cmpint (state.calls, ==, 0);
    }
  return dex_future_new_true ();
}

int
main (int argc, char *argv[])
{
  DexFuture *finished;

  dex_init ();
  finished = dex_scheduler_spawn (NULL, 0, consume, NULL, NULL);
  while (dex_future_is_pending (finished))
    g_main_context_iteration (NULL, TRUE);
  g_assert_true (dex_await_boolean (finished, NULL));
  return 0;
}
