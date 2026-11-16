/* test-generator.c
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
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef G_OS_WIN32
# include <io.h>
# include <fcntl.h>
# define pipe(fds) _pipe (fds, 4096, _O_BINARY)
#endif

#include "dex-future-private.h"

typedef struct
{
  DexGenerator *generator;
  DexFuture    *item;
  DexPromise   *external;
  int           calls;
  int           destroyed;
  int           position;
  gboolean      fail;
  GThread      *thread;
} State;

static void
destroy_state (gpointer data)
{
  State *state = data;

  state->destroyed++;
  state->thread = g_thread_self ();
}

static DexFuture *
ordinary (gpointer data)
{
  State *state = data;

  state->calls++;
  return g_steal_pointer (&state->item);
}

static DexFuture *
optional_work (gpointer data)
{
  int *calls = data;

  (*calls)++;
  return dex_future_new_true ();
}

static void
test_ordinary (void)
{
  State state = { 0 };
  DexGenerator *generator = dex_generator_new (ordinary, &state, destroy_state);
  DexFuture *first;
  DexFuture *second;
  int calls = 0;

  state.item = DEX_FUTURE (dex_deferred_new (NULL, optional_work, &calls, NULL));
  first = state.item;
  g_assert_true (dex_generator_next (generator) == first);
  state.item = dex_future_new_for_int (42);
  second = dex_generator_next (generator);
  g_assert_cmpint (calls, ==, 0);
  g_assert_null (dex_generator_next (generator));
  g_assert_true (dex_generator_is_exhausted (generator));
  g_assert_null (dex_generator_next (generator));
  g_assert_cmpint (state.calls, ==, 3);
  g_assert_cmpint (state.destroyed, ==, 0);
  dex_generator_close (generator);
  g_assert_null (dex_generator_next (generator));
  dex_unref (generator);
  g_assert_cmpint (state.destroyed, ==, 1);
  g_assert_cmpint (dex_await_int (second, NULL), ==, 42);
  dex_unref (first);
  g_assert_cmpint (calls, ==, 0);
}

static DexFuture *
recursive (gpointer data)
{
  State *state = data;
  DexFuture *nested = dex_generator_next (state->generator);
  GError *error = NULL;

  dex_future_get_value (nested, &error);
  g_assert_error (error, DEX_ERROR, DEX_ERROR_INVALID_TRANSITION);
  g_clear_error (&error);
  dex_unref (nested);
  dex_generator_close (state->generator);
  g_assert_cmpint (state->destroyed, ==, 0);
  return dex_future_new_false ();
}

static void
test_recursive (void)
{
  State state = { 0 };
  DexFuture *drain;
  DexFuture *again;
  GError *error = NULL;

  state.generator = dex_generator_new (recursive, &state, destroy_state);
  g_assert_false (dex_await_boolean (dex_generator_next (state.generator), NULL));
  g_assert_false (dex_generator_is_exhausted (state.generator));
  g_assert_cmpint (state.destroyed, ==, 1);
  drain = dex_generator_close_after_drain (state.generator);
  again = dex_generator_close_after_drain (state.generator);
  g_assert_true (drain == again);
  g_assert_true (dex_await_boolean (drain, NULL));
  dex_unref (again);
  dex_await (dex_generator_next (state.generator), &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CLOSED);
  g_clear_error (&error);
  dex_unref (state.generator);
}

static DexFuture *
fiber_producer (gpointer data)
{
  State *state = data;
  GError *error = NULL;

  state->calls++;
  if (state->external != NULL && !dex_await (dex_ref (state->external), &error))
    return dex_future_new_for_error (error);
  for (state->position = 0; state->position < 3; state->position++)
    {
      if (!dex_fiber_yield_int (state->position, &error))
        return dex_future_new_for_error (error);
    }
  if (state->fail)
    return dex_future_new_reject (G_IO_ERROR, G_IO_ERROR_FAILED, "producer failed");
  return dex_future_new_true ();
}

static DexFuture *
coroutine_producer (DexCoroutineContext *context,
                    gpointer             data)
{
  State *state = data;
  GError *error = NULL;

  DEX_COROUTINE_BEGIN (context);
  state->calls++;
  if (state->external != NULL)
    {
      DEX_COROUTINE_SUSPEND (dex_ref (state->external), &error);
      if (error != NULL)
        return dex_future_new_for_error (error);
    }
  while (state->position < 3)
    {
      DEX_COROUTINE_YIELD_INT (state->position++, &error);
      if (error != NULL)
        return dex_future_new_for_error (error);
    }
  if (state->fail)
    return dex_future_new_reject (G_IO_ERROR, G_IO_ERROR_FAILED, "producer failed");
  return dex_future_new_true ();
  DEX_COROUTINE_END;
}

static DexGenerator *
adapter (State    *state,
         gboolean  coroutine)
{
  if (coroutine)
    return dex_generator_new_coroutine (NULL, coroutine_producer, state, destroy_state);
  return dex_generator_new_fiber (NULL, 0, fiber_producer, state, destroy_state);
}

static void
test_adapters (void)
{
  for (guint kind = 0; kind < 2; kind++)
    for (guint failure = 0; failure < 2; failure++)
      {
        State state = { .fail = failure };
        DexGenerator *generator = adapter (&state, kind);
        GError *error = NULL;
        int value = -1;

        g_assert_cmpint (state.calls, ==, 0);
        for (int i = 0; i < 3; i++)
          {
            g_assert_true (dex_generator_await_next_int (generator, &value, &error));
            g_assert_no_error (error);
            g_assert_cmpint (value, ==, i);
            g_assert_false (dex_generator_is_exhausted (generator));
          }
        g_assert_false (dex_generator_await_next_int (generator, &value, &error));
        g_assert_cmpint (value, ==, 0);
        g_assert_cmpint (state.calls, ==, 1);
        if (failure)
          {
            g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
            g_clear_error (&error);
            g_assert_false (dex_generator_is_exhausted (generator));
            g_assert_false (dex_generator_await_next_int (generator, &value, &error));
            g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
            g_clear_error (&error);
          }
        else
          {
            g_assert_no_error (error);
            g_assert_true (dex_generator_is_exhausted (generator));
            g_assert_null (dex_generator_next (generator));
          }
        g_assert_true (dex_await_boolean (dex_generator_close_after_drain (generator), &error));
        g_assert_no_error (error);
        g_assert_cmpint (state.destroyed, ==, 1);
        dex_unref (generator);
      }
}

static void
test_shutdown (void)
{
  for (guint kind = 0; kind < 2; kind++)
    for (guint mode = 0; mode < 5; mode++)
      {
        State state = { 0 };
        DexGenerator *generator = adapter (&state, kind);
        DexFuture *pull = NULL;
        DexFuture *drain;
        GError *error = NULL;
        int value;

        if (mode == 1)
          g_assert_true (dex_generator_await_next_int (generator, &value, NULL));
        else if (mode >= 2)
          {
            pull = dex_generator_next (generator);
            if (mode == 2)
              dex_clear (&pull);
            else if (mode == 3)
              dex_future_abandon (pull);
          }

        if (mode == 4)
          {
            dex_unref (generator);
            dex_await (pull, &error);
            g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
            g_clear_error (&error);
            while (state.destroyed == 0)
              g_assert_true (dex_fiber_yield (NULL));
          }
        else
          {
            drain = dex_generator_close_after_drain (generator);
            g_assert_true (dex_await_boolean (drain, NULL));
            g_assert_false (dex_generator_is_exhausted (generator));
            dex_clear (&pull);
            dex_unref (generator);
          }
        g_assert_cmpint (state.destroyed, ==, 1);
        g_assert_true (state.thread == g_thread_self ());
      }
}

static void
test_pending_cleanup (void)
{
  for (guint kind = 0; kind < 2; kind++)
    {
      State state = { 0 };
      DexGenerator *generator;
      DexFuture *pull;
      DexFuture *overlap;
      DexFuture *drain;
      GError *error = NULL;

      state.external = dex_promise_new ();
      generator = adapter (&state, kind);
      pull = dex_generator_next (generator);
      overlap = dex_generator_next (generator);
      dex_await (overlap, &error);
      g_assert_error (error, DEX_ERROR, DEX_ERROR_INVALID_TRANSITION);
      g_clear_error (&error);
      while (state.calls == 0)
        g_assert_true (dex_fiber_yield (NULL));
      drain = dex_generator_close_after_drain (generator);
      dex_await (pull, &error);
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
      g_clear_error (&error);
      if (kind)
        {
          g_assert_cmpint (state.destroyed, ==, 0);
          g_assert_true (dex_future_is_pending (drain));
        }
      dex_promise_resolve_boolean (state.external, TRUE);
      g_assert_true (dex_await_boolean (drain, NULL));
      g_assert_cmpint (state.destroyed, ==, 1);
      dex_unref (state.external);
      dex_unref (generator);
    }
}

static void
test_yield_context (void)
{
  GError *error = NULL;

  g_assert_false (dex_fiber_yield_int (1, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_NO_FIBER);
  g_clear_error (&error);
}

static void
test_unrelated_fiber (void)
{
  GError *error = NULL;

  g_assert_false (dex_fiber_yield_int (1, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_INVALID_TRANSITION);
  g_clear_error (&error);
}

static void
test_type_boolean (void)
{
  State state = { 0 };
  DexGenerator *generator = dex_generator_new (ordinary, &state, NULL);
  GError *error = NULL;
  gboolean value;

  state.item = dex_future_new_false ();
  g_assert_true (dex_generator_await_next_boolean (generator, &value, &error));
  g_assert_no_error (error);
  g_assert_cmpfloat (value, ==, FALSE);
  state.item = dex_future_new_for_string ("mismatch");
  g_assert_false (dex_generator_await_next_boolean (generator, &value, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_TYPE_MISMATCH);
  g_clear_error (&error);
  g_assert_cmpfloat (value, ==, FALSE);
  g_assert_false (dex_generator_await_next_boolean (generator, &value, &error));
  g_assert_no_error (error);
  g_assert_cmpfloat (value, ==, FALSE);
  dex_unref (generator);
}

static void
test_type_int (void)
{
  State state = { 0 };
  DexGenerator *generator = dex_generator_new (ordinary, &state, NULL);
  GError *error = NULL;
  int value;

  state.item = dex_future_new_for_int (0);
  g_assert_true (dex_generator_await_next_int (generator, &value, &error));
  g_assert_no_error (error);
  g_assert_cmpfloat (value, ==, 0);
  state.item = dex_future_new_for_string ("mismatch");
  g_assert_false (dex_generator_await_next_int (generator, &value, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_TYPE_MISMATCH);
  g_clear_error (&error);
  g_assert_cmpfloat (value, ==, 0);
  g_assert_false (dex_generator_await_next_int (generator, &value, &error));
  g_assert_no_error (error);
  g_assert_cmpfloat (value, ==, 0);
  dex_unref (generator);
}

static void
test_type_uint (void)
{
  State state = { 0 };
  DexGenerator *generator = dex_generator_new (ordinary, &state, NULL);
  GError *error = NULL;
  guint value;

  state.item = dex_future_new_for_uint (0);
  g_assert_true (dex_generator_await_next_uint (generator, &value, &error));
  g_assert_no_error (error);
  g_assert_cmpfloat (value, ==, 0);
  state.item = dex_future_new_for_string ("mismatch");
  g_assert_false (dex_generator_await_next_uint (generator, &value, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_TYPE_MISMATCH);
  g_clear_error (&error);
  g_assert_cmpfloat (value, ==, 0);
  g_assert_false (dex_generator_await_next_uint (generator, &value, &error));
  g_assert_no_error (error);
  g_assert_cmpfloat (value, ==, 0);
  dex_unref (generator);
}

static void
test_type_int64 (void)
{
  State state = { 0 };
  DexGenerator *generator = dex_generator_new (ordinary, &state, NULL);
  GError *error = NULL;
  gint64 value;

  state.item = dex_future_new_for_int64 (0);
  g_assert_true (dex_generator_await_next_int64 (generator, &value, &error));
  g_assert_no_error (error);
  g_assert_cmpfloat (value, ==, 0);
  state.item = dex_future_new_for_string ("mismatch");
  g_assert_false (dex_generator_await_next_int64 (generator, &value, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_TYPE_MISMATCH);
  g_clear_error (&error);
  g_assert_cmpfloat (value, ==, 0);
  g_assert_false (dex_generator_await_next_int64 (generator, &value, &error));
  g_assert_no_error (error);
  g_assert_cmpfloat (value, ==, 0);
  dex_unref (generator);
}

static void
test_type_uint64 (void)
{
  State state = { 0 };
  DexGenerator *generator = dex_generator_new (ordinary, &state, NULL);
  GError *error = NULL;
  guint64 value;

  state.item = dex_future_new_for_uint64 (0);
  g_assert_true (dex_generator_await_next_uint64 (generator, &value, &error));
  g_assert_no_error (error);
  g_assert_cmpfloat (value, ==, 0);
  state.item = dex_future_new_for_string ("mismatch");
  g_assert_false (dex_generator_await_next_uint64 (generator, &value, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_TYPE_MISMATCH);
  g_clear_error (&error);
  g_assert_cmpfloat (value, ==, 0);
  g_assert_false (dex_generator_await_next_uint64 (generator, &value, &error));
  g_assert_no_error (error);
  g_assert_cmpfloat (value, ==, 0);
  dex_unref (generator);
}

static void
test_type_float (void)
{
  State state = { 0 };
  DexGenerator *generator = dex_generator_new (ordinary, &state, NULL);
  GError *error = NULL;
  float value;

  state.item = dex_future_new_for_float (0);
  g_assert_true (dex_generator_await_next_float (generator, &value, &error));
  g_assert_no_error (error);
  g_assert_cmpfloat (value, ==, 0);
  state.item = dex_future_new_for_string ("mismatch");
  g_assert_false (dex_generator_await_next_float (generator, &value, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_TYPE_MISMATCH);
  g_clear_error (&error);
  g_assert_cmpfloat (value, ==, 0);
  g_assert_false (dex_generator_await_next_float (generator, &value, &error));
  g_assert_no_error (error);
  g_assert_cmpfloat (value, ==, 0);
  dex_unref (generator);
}

static void
test_type_double (void)
{
  State state = { 0 };
  DexGenerator *generator = dex_generator_new (ordinary, &state, NULL);
  GError *error = NULL;
  double value;

  state.item = dex_future_new_for_double (0);
  g_assert_true (dex_generator_await_next_double (generator, &value, &error));
  g_assert_no_error (error);
  g_assert_cmpfloat (value, ==, 0);
  state.item = dex_future_new_for_string ("mismatch");
  g_assert_false (dex_generator_await_next_double (generator, &value, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_TYPE_MISMATCH);
  g_clear_error (&error);
  g_assert_cmpfloat (value, ==, 0);
  g_assert_false (dex_generator_await_next_double (generator, &value, &error));
  g_assert_no_error (error);
  g_assert_cmpfloat (value, ==, 0);
  dex_unref (generator);
}

static void
test_type_enum (void)
{
  State state = { 0 };
  DexGenerator *generator = dex_generator_new (ordinary, &state, NULL);
  GError *error = NULL;
  int value;

  state.item = dex_future_new_enum (G_TYPE_IO_ERROR_ENUM, 0);
  g_assert_true (dex_generator_await_next_enum (generator, G_TYPE_IO_ERROR_ENUM, &value, &error));
  g_assert_no_error (error);
  g_assert_cmpfloat (value, ==, 0);
  state.item = dex_future_new_for_string ("mismatch");
  g_assert_false (dex_generator_await_next_enum (generator, G_TYPE_IO_ERROR_ENUM, &value, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_TYPE_MISMATCH);
  g_clear_error (&error);
  g_assert_cmpfloat (value, ==, 0);
  g_assert_false (dex_generator_await_next_enum (generator, G_TYPE_IO_ERROR_ENUM, &value, &error));
  g_assert_no_error (error);
  g_assert_cmpfloat (value, ==, 0);
  dex_unref (generator);
}

static void
test_type_flags (void)
{
  State state = { 0 };
  DexGenerator *generator = dex_generator_new (ordinary, &state, NULL);
  GError *error = NULL;
  guint value;

  {
    GValue flags = G_VALUE_INIT;

    g_value_init (&flags, G_TYPE_APPLICATION_FLAGS);
    state.item = dex_future_new_for_value (&flags);
    g_value_unset (&flags);
  }
  g_assert_true (dex_generator_await_next_flags (generator, G_TYPE_APPLICATION_FLAGS, &value, &error));
  g_assert_no_error (error);
  g_assert_cmpfloat (value, ==, 0);
  state.item = dex_future_new_for_string ("mismatch");
  g_assert_false (dex_generator_await_next_flags (generator, G_TYPE_APPLICATION_FLAGS, &value, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_TYPE_MISMATCH);
  g_clear_error (&error);
  g_assert_cmpfloat (value, ==, 0);
  g_assert_false (dex_generator_await_next_flags (generator, G_TYPE_APPLICATION_FLAGS, &value, &error));
  g_assert_no_error (error);
  g_assert_cmpfloat (value, ==, 0);
  dex_unref (generator);
}

static void
test_type_string (void)
{
  State state = { 0 };
  DexGenerator *generator = dex_generator_new (ordinary, &state, NULL);
  GError *error = NULL;
  char *value;

  state.item = dex_future_new_for_string (NULL);
  g_assert_true (dex_generator_await_next_string (generator, &value, &error));
  g_assert_no_error (error);
  g_assert_null (value);
  state.item = dex_future_new_for_int (1);
  g_assert_false (dex_generator_await_next_string (generator, &value, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_TYPE_MISMATCH);
  g_clear_error (&error);
  g_assert_null (value);
  g_assert_false (dex_generator_await_next_string (generator, &value, &error));
  g_assert_no_error (error);
  g_assert_null (value);
  dex_unref (generator);
}

static void
test_type_variant (void)
{
  State state = { 0 };
  DexGenerator *generator = dex_generator_new (ordinary, &state, NULL);
  GError *error = NULL;
  GVariant *value;

  state.item = dex_future_new_take_variant (NULL);
  g_assert_true (dex_generator_await_next_variant (generator, &value, &error));
  g_assert_no_error (error);
  g_assert_null (value);
  state.item = dex_future_new_for_string ("mismatch");
  g_assert_false (dex_generator_await_next_variant (generator, &value, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_TYPE_MISMATCH);
  g_clear_error (&error);
  g_assert_null (value);
  g_assert_false (dex_generator_await_next_variant (generator, &value, &error));
  g_assert_no_error (error);
  g_assert_null (value);
  dex_unref (generator);
}

static void
test_type_boxed (void)
{
  State state = { 0 };
  DexGenerator *generator = dex_generator_new (ordinary, &state, NULL);
  GError *error = NULL;
  gpointer value;

  state.item = dex_future_new_take_boxed (G_TYPE_BYTES, NULL);
  g_assert_true (dex_generator_await_next_boxed (generator, G_TYPE_BYTES, &value, &error));
  g_assert_no_error (error);
  g_assert_null (value);
  state.item = dex_future_new_for_string ("mismatch");
  g_assert_false (dex_generator_await_next_boxed (generator, G_TYPE_BYTES, &value, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_TYPE_MISMATCH);
  g_clear_error (&error);
  g_assert_null (value);
  g_assert_false (dex_generator_await_next_boxed (generator, G_TYPE_BYTES, &value, &error));
  g_assert_no_error (error);
  g_assert_null (value);
  dex_unref (generator);
}

static void
test_type_pointer (void)
{
  State state = { 0 };
  DexGenerator *generator = dex_generator_new (ordinary, &state, NULL);
  GError *error = NULL;
  gpointer value;

  state.item = dex_future_new_for_pointer (NULL);
  g_assert_true (dex_generator_await_next_pointer (generator, &value, &error));
  g_assert_no_error (error);
  g_assert_null (value);
  state.item = dex_future_new_for_string ("mismatch");
  g_assert_false (dex_generator_await_next_pointer (generator, &value, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_TYPE_MISMATCH);
  g_clear_error (&error);
  g_assert_null (value);
  g_assert_false (dex_generator_await_next_pointer (generator, &value, &error));
  g_assert_no_error (error);
  g_assert_null (value);
  dex_unref (generator);
}

static void
test_type_object (void)
{
  State state = { 0 };
  DexGenerator *generator = dex_generator_new (ordinary, &state, NULL);
  GError *error = NULL;
  GObject *value;

  {
    GValue item = G_VALUE_INIT;

    g_value_init (&item, G_TYPE_OBJECT);
    state.item = dex_future_new_for_value (&item);
    g_value_unset (&item);
  }
  g_assert_true (dex_generator_await_next_object (generator, &value, &error));
  g_assert_no_error (error);
  g_assert_null (value);
  state.item = dex_future_new_for_string ("mismatch");
  g_assert_false (dex_generator_await_next_object (generator, &value, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_TYPE_MISMATCH);
  g_clear_error (&error);
  g_assert_null (value);
  g_assert_false (dex_generator_await_next_object (generator, &value, &error));
  g_assert_no_error (error);
  g_assert_null (value);
  dex_unref (generator);
}

static void
test_type_future (void)
{
  State state = { 0 };
  DexGenerator *generator = dex_generator_new (ordinary, &state, NULL);
  GError *error = NULL;
  DexFuture *value;
  GValue item = G_VALUE_INIT;
  GValue result = G_VALUE_INIT;
  DexChannel *channel = dex_channel_new (1);

  g_value_init (&item, DEX_TYPE_FUTURE);
  state.item = dex_future_new_for_value (&item);
  g_value_unset (&item);
  g_assert_true (dex_generator_await_next_future (generator, &value, &error));
  g_assert_no_error (error);
  g_assert_null (value);
  state.item = dex_future_new_for_string ("mismatch");
  g_assert_false (dex_generator_await_next_future (generator, &value, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_TYPE_MISMATCH);
  g_clear_error (&error);
  g_assert_null (value);
  g_value_init (&item, DEX_TYPE_CHANNEL);
  dex_value_set_object (&item, DEX_OBJECT (channel));
  state.item = dex_future_new_for_value (&item);
  g_assert_false (dex_generator_await_next_future (generator, &value, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_TYPE_MISMATCH);
  g_clear_error (&error);
  g_assert_null (value);

  state.item = dex_future_new_for_value (&item);
  g_value_unset (&item);
  dex_unref (channel);
  g_assert_true (dex_generator_await_next (generator, &result, &error));
  g_assert_no_error (error);
  g_assert_true (G_VALUE_HOLDS (&result, DEX_TYPE_CHANNEL));
  g_assert_true (DEX_IS_CHANNEL (dex_value_get_object (&result)));
  g_value_unset (&result);

  g_assert_false (dex_generator_await_next_future (generator, &value, &error));
  g_assert_no_error (error);
  g_assert_null (value);
  dex_unref (generator);
}

static void
test_owned_values (void)
{
  State state = { 0 };
  DexGenerator *generator = dex_generator_new (ordinary, &state, NULL);
  GObject *object = g_object_new (G_TYPE_OBJECT, NULL);
  GObject *out = NULL;
  DexFuture *dex_out = NULL;
  GBytes *bytes = g_bytes_new_static ("abc", 3);
  gpointer boxed = NULL;
  GVariant *variant = NULL;
  char *string = NULL;
  GValue value = G_VALUE_INIT;
  int fd;
  int pipes[2];
  int calls = 0;
  DexDeferred *deferred = dex_deferred_new (NULL, optional_work, &calls, NULL);

  state.item = dex_future_new_for_object (object);
  g_assert_true (dex_generator_await_next_object (generator, &out, NULL));
  g_assert_true (out == object);
  g_object_unref (object);
  g_object_unref (out);
  g_value_init (&value, DEX_TYPE_FUTURE);
  dex_value_set_object (&value, DEX_OBJECT (deferred));
  state.item = dex_future_new_for_value (&value);
  g_value_unset (&value);
  g_assert_true (dex_generator_await_next_future (generator, &dex_out, NULL));
  g_assert_true (dex_out == DEX_FUTURE (deferred));
  dex_unref (dex_out);
  dex_unref (deferred);
  g_assert_cmpint (calls, ==, 0);
  state.item = dex_future_new_take_boxed (G_TYPE_BYTES, bytes);
  g_assert_true (dex_generator_await_next_boxed (generator, G_TYPE_BYTES, &boxed, NULL));
  g_assert_cmpuint (g_bytes_get_size (boxed), ==, 3);
  g_bytes_unref (boxed);
  state.item = dex_future_new_take_variant (g_variant_new_int32 (12));
  g_assert_true (dex_generator_await_next_variant (generator, &variant, NULL));
  g_assert_cmpint (g_variant_get_int32 (variant), ==, 12);
  g_variant_unref (variant);
  state.item = dex_future_new_for_string ("hello");
  g_assert_true (dex_generator_await_next_string (generator, &string, NULL));
  g_assert_cmpstr (string, ==, "hello");
  g_free (string);
  g_assert_cmpint (pipe (pipes), ==, 0);
  state.item = dex_future_new_for_fd (pipes[0]);
  g_assert_true (dex_generator_await_next_fd (generator, &fd, NULL));
  g_assert_cmpint (fd, ==, pipes[0]);
  close (fd);
  close (pipes[1]);
  state.item = dex_future_new_for_int (7);
  g_assert_true (dex_generator_await_next (generator, &value, NULL));
  g_assert_cmpint (g_value_get_int (&value), ==, 7);
  g_value_unset (&value);
  g_assert_false (dex_generator_await_next (generator, &value, NULL));
  g_assert_cmpuint (G_VALUE_TYPE (&value), ==, 0);
  dex_unref (generator);
}

typedef struct
{
  DexFuture     parent_instance;
  DexGenerator *generator;
  DexFuture    *pull;
  int           count;
} ReentrantConsumer;

typedef struct
{
  DexFutureClass parent_class;
} ReentrantConsumerClass;

static GType reentrant_consumer_get_type (void);
DEX_DEFINE_FINAL_TYPE (ReentrantConsumer, reentrant_consumer, DEX_TYPE_FUTURE)

static gboolean
reentrant_consumer_propagate (DexFuture *future,
                              DexFuture *completed)
{
  ReentrantConsumer *self = (ReentrantConsumer *)future;
  const GValue *value = dex_future_get_value (completed, NULL);

  dex_clear (&self->pull);
  g_assert_nonnull (value);
  if (dex_generator_is_exhausted (self->generator))
    {
      dex_future_complete_from (future, completed);
    }
  else
    {
      g_assert_cmpint (g_value_get_int (value), ==, self->count++);
      self->pull = dex_generator_next (self->generator);
      dex_future_chain (self->pull, future);
    }
  return TRUE;
}

static void
reentrant_consumer_class_init (ReentrantConsumerClass *klass)
{
  DEX_FUTURE_CLASS (klass)->propagate = reentrant_consumer_propagate;
}

static void
reentrant_consumer_init (ReentrantConsumer *self)
{
}

static void
test_reentry (void)
{
  for (guint kind = 0; kind < 2; kind++)
    {
      State state = { 0 };
      ReentrantConsumer *consumer;

      consumer = (ReentrantConsumer *)dex_object_create_instance (reentrant_consumer_get_type ());
      consumer->generator = adapter (&state, kind);
      consumer->pull = dex_generator_next (consumer->generator);
      dex_future_chain (consumer->pull, DEX_FUTURE (consumer));
      g_assert_true (dex_await_boolean (dex_ref (consumer), NULL));
      g_assert_cmpint (consumer->count, ==, 3);
      g_assert_true (dex_await_boolean (dex_generator_close_after_drain (consumer->generator), NULL));
      dex_unref (consumer->generator);
      dex_unref (consumer);
    }
}

static gpointer
close_thread (gpointer data)
{
  dex_generator_close (data);
  return NULL;
}

static void
test_cross_thread (void)
{
  for (guint kind = 0; kind < 2; kind++)
    for (guint started = 0; started < 2; started++)
      {
        State state = { 0 };
        DexGenerator *generator = adapter (&state, kind);
        GThread *thread;
        int value;

        if (started)
          g_assert_true (dex_generator_await_next_int (generator, &value, NULL));
        thread = g_thread_new ("generator-close", close_thread, generator);
        g_thread_join (thread);
        g_assert_true (dex_await_boolean (dex_generator_close_after_drain (generator), NULL));
        g_assert_cmpint (state.destroyed, ==, 1);
        g_assert_true (state.thread == g_thread_self ());
        dex_unref (generator);
      }
}

static void
test_interleaved (void)
{
  State states[4] = { 0 };
  DexGenerator *generators[4];

  for (guint i = 0; i < 4; i++)
    generators[i] = adapter (&states[i], i % 2);
  for (int n = 0; n < 3; n++)
    for (guint i = 0; i < 4; i++)
      {
        int value;

        g_assert_true (dex_generator_await_next_int (generators[i], &value, NULL));
        g_assert_cmpint (value, ==, n);
      }
  for (guint i = 0; i < 4; i++)
    {
      g_assert_true (dex_await_boolean (dex_generator_close_after_drain (generators[i]), NULL));
      dex_unref (generators[i]);
    }
}

typedef struct
{
  GMutex        mutex;
  GCond         cond;
  gboolean      entered;
  gboolean      released;
  int           destroyed;
  DexGenerator *generator;
} BlockingState;

static DexFuture *
blocking_producer (gpointer data)
{
  BlockingState *state = data;

  g_mutex_lock (&state->mutex);
  state->entered = TRUE;
  g_cond_signal (&state->cond);
  while (!state->released)
    g_cond_wait (&state->cond, &state->mutex);
  g_mutex_unlock (&state->mutex);
  return dex_future_new_for_int (123);
}

static void
blocking_destroy (gpointer data)
{
  BlockingState *state = data;

  state->destroyed++;
}

static gpointer
blocking_thread (gpointer data)
{
  BlockingState *state = data;

  return dex_generator_next (state->generator);
}

static void
test_active_close (void)
{
  BlockingState state = { 0 };
  DexFuture *drain;
  DexFuture *result;
  GThread *thread;
  GError *error = NULL;

  g_mutex_init (&state.mutex);
  g_cond_init (&state.cond);
  state.generator = dex_generator_new (blocking_producer, &state, blocking_destroy);
  thread = g_thread_new ("generator-producer", blocking_thread, &state);
  g_mutex_lock (&state.mutex);
  while (!state.entered)
    g_cond_wait (&state.cond, &state.mutex);
  g_mutex_unlock (&state.mutex);
  dex_await (dex_generator_next (state.generator), &error);
  g_assert_error (error, DEX_ERROR, DEX_ERROR_INVALID_TRANSITION);
  g_clear_error (&error);
  drain = dex_generator_close_after_drain (state.generator);
  g_assert_true (dex_future_is_pending (drain));
  g_assert_cmpint (state.destroyed, ==, 0);
  dex_unref (state.generator);
  g_mutex_lock (&state.mutex);
  state.released = TRUE;
  g_cond_signal (&state.cond);
  g_mutex_unlock (&state.mutex);
  result = g_thread_join (thread);
  g_assert_true (dex_await_boolean (drain, NULL));
  g_assert_cmpint (state.destroyed, ==, 1);
  g_assert_cmpint (dex_await_int (result, NULL), ==, 123);
  g_mutex_clear (&state.mutex);
  g_cond_clear (&state.cond);
}

typedef struct
{
  GObject     *object;
  DexDeferred *deferred;
  GVariant    *variant;
  GBytes      *bytes;
  int          fd;
} YieldState;

static DexFuture *
typed_fiber (gpointer data)
{
  YieldState *state = data;
  GError *error = NULL;

  if (!dex_fiber_yield_boolean (FALSE, &error))
    return dex_future_new_for_error (error);

  if (!dex_fiber_yield_int (-42, &error))
    return dex_future_new_for_error (error);

  if (!dex_fiber_yield_uint (42, &error))
    return dex_future_new_for_error (error);

  if (!dex_fiber_yield_int64 (-42, &error))
    return dex_future_new_for_error (error);

  if (!dex_fiber_yield_uint64 (42, &error))
    return dex_future_new_for_error (error);

  if (!dex_fiber_yield_float (1.5, &error))
    return dex_future_new_for_error (error);

  if (!dex_fiber_yield_double (1.5, &error))
    return dex_future_new_for_error (error);

  if (!dex_fiber_yield_enum (G_TYPE_IO_ERROR_ENUM, G_IO_ERROR_FAILED, &error))
    return dex_future_new_for_error (error);

  if (!dex_fiber_yield_flags (G_TYPE_APPLICATION_FLAGS, G_APPLICATION_NON_UNIQUE, &error))
    return dex_future_new_for_error (error);

  if (!dex_fiber_yield_string ("hello", &error))
    return dex_future_new_for_error (error);

  if (!dex_fiber_yield_pointer (state, &error))
    return dex_future_new_for_error (error);

  if (!dex_fiber_yield_object (state->object, &error))
    return dex_future_new_for_error (error);

  if (!dex_fiber_yield_future (DEX_FUTURE (state->deferred), &error))
    return dex_future_new_for_error (error);

  if (!dex_fiber_yield_variant (state->variant, &error))
    return dex_future_new_for_error (error);

  if (!dex_fiber_yield_boxed (G_TYPE_BYTES, state->bytes, &error))
    return dex_future_new_for_error (error);

  if (!dex_fiber_yield_fd (g_steal_fd (&state->fd), &error))
    return dex_future_new_for_error (error);
  return dex_future_new_true ();
}

static DexFuture *
typed_coroutine (DexCoroutineContext *context,
                 gpointer             data)
{
  YieldState *state = data;
  GError *error = NULL;

  DEX_COROUTINE_BEGIN (context);
  DEX_COROUTINE_YIELD_BOOLEAN (FALSE, &error);
  if (error != NULL)
    return dex_future_new_for_error (error);
  DEX_COROUTINE_YIELD_INT (-42, &error);
  if (error != NULL)
    return dex_future_new_for_error (error);
  DEX_COROUTINE_YIELD_UINT (42, &error);
  if (error != NULL)
    return dex_future_new_for_error (error);
  DEX_COROUTINE_YIELD_INT64 (-42, &error);
  if (error != NULL)
    return dex_future_new_for_error (error);
  DEX_COROUTINE_YIELD_UINT64 (42, &error);
  if (error != NULL)
    return dex_future_new_for_error (error);
  DEX_COROUTINE_YIELD_FLOAT (1.5, &error);
  if (error != NULL)
    return dex_future_new_for_error (error);
  DEX_COROUTINE_YIELD_DOUBLE (1.5, &error);
  if (error != NULL)
    return dex_future_new_for_error (error);
  DEX_COROUTINE_YIELD_ENUM (G_TYPE_IO_ERROR_ENUM, G_IO_ERROR_FAILED, &error);
  if (error != NULL)
    return dex_future_new_for_error (error);
  DEX_COROUTINE_YIELD_FLAGS (G_TYPE_APPLICATION_FLAGS, G_APPLICATION_NON_UNIQUE, &error);
  if (error != NULL)
    return dex_future_new_for_error (error);
  DEX_COROUTINE_YIELD_STRING ("hello", &error);
  if (error != NULL)
    return dex_future_new_for_error (error);
  DEX_COROUTINE_YIELD_POINTER (state, &error);
  if (error != NULL)
    return dex_future_new_for_error (error);
  DEX_COROUTINE_YIELD_OBJECT (state->object, &error);
  if (error != NULL)
    return dex_future_new_for_error (error);
  DEX_COROUTINE_YIELD_FUTURE (DEX_FUTURE (state->deferred), &error);
  if (error != NULL)
    return dex_future_new_for_error (error);
  DEX_COROUTINE_YIELD_VARIANT (state->variant, &error);
  if (error != NULL)
    return dex_future_new_for_error (error);
  DEX_COROUTINE_YIELD_BOXED (G_TYPE_BYTES, state->bytes, &error);
  if (error != NULL)
    return dex_future_new_for_error (error);
  DEX_COROUTINE_YIELD_FD (g_steal_fd (&state->fd), &error);
  if (error != NULL)
    return dex_future_new_for_error (error);
  return dex_future_new_true ();
  DEX_COROUTINE_END;
}

static void
test_typed_yields (void)
{
  for (guint kind = 0; kind < 2; kind++)
    {
      YieldState storage = { 0 };
      YieldState *state = &storage;
      DexGenerator *generator;
      GValue value = G_VALUE_INIT;
      int calls = 0;
      int pipes[2];
      int fd;
      char byte;

      state->object = g_object_new (G_TYPE_OBJECT, NULL);
      state->deferred = dex_deferred_new (NULL, optional_work, &calls, NULL);
      state->variant = g_variant_ref_sink (g_variant_new_int32 (1));
      state->bytes = g_bytes_new_static ("abc", 3);
      g_assert_cmpint (pipe (pipes), ==, 0);
      state->fd = pipes[0];
      if (kind)
        generator = dex_generator_new_coroutine (NULL, typed_coroutine, state, NULL);
      else
        generator = dex_generator_new_fiber (NULL, 0, typed_fiber, state, NULL);

      g_assert_true (dex_generator_await_next (generator, &value, NULL));
      g_assert_true (G_VALUE_HOLDS (&value, G_TYPE_BOOLEAN));
      g_assert_cmpfloat (g_value_get_boolean (&value), ==, 0);
      g_value_unset (&value);

      g_assert_true (dex_generator_await_next (generator, &value, NULL));
      g_assert_true (G_VALUE_HOLDS (&value, G_TYPE_INT));
      g_assert_cmpfloat (g_value_get_int (&value), ==, -42);
      g_value_unset (&value);

      g_assert_true (dex_generator_await_next (generator, &value, NULL));
      g_assert_true (G_VALUE_HOLDS (&value, G_TYPE_UINT));
      g_assert_cmpfloat (g_value_get_uint (&value), ==, 42);
      g_value_unset (&value);

      g_assert_true (dex_generator_await_next (generator, &value, NULL));
      g_assert_true (G_VALUE_HOLDS (&value, G_TYPE_INT64));
      g_assert_cmpfloat (g_value_get_int64 (&value), ==, -42);
      g_value_unset (&value);

      g_assert_true (dex_generator_await_next (generator, &value, NULL));
      g_assert_true (G_VALUE_HOLDS (&value, G_TYPE_UINT64));
      g_assert_cmpfloat (g_value_get_uint64 (&value), ==, 42);
      g_value_unset (&value);

      g_assert_true (dex_generator_await_next (generator, &value, NULL));
      g_assert_true (G_VALUE_HOLDS (&value, G_TYPE_FLOAT));
      g_assert_cmpfloat (g_value_get_float (&value), ==, 1.5);
      g_value_unset (&value);

      g_assert_true (dex_generator_await_next (generator, &value, NULL));
      g_assert_true (G_VALUE_HOLDS (&value, G_TYPE_DOUBLE));
      g_assert_cmpfloat (g_value_get_double (&value), ==, 1.5);
      g_value_unset (&value);

      g_assert_true (dex_generator_await_next (generator, &value, NULL));
      g_assert_true (G_VALUE_HOLDS (&value, G_TYPE_IO_ERROR_ENUM));
      g_assert_cmpfloat (g_value_get_enum (&value), ==, G_IO_ERROR_FAILED);
      g_value_unset (&value);

      g_assert_true (dex_generator_await_next (generator, &value, NULL));
      g_assert_true (G_VALUE_HOLDS (&value, G_TYPE_APPLICATION_FLAGS));
      g_assert_cmpfloat (g_value_get_flags (&value), ==, G_APPLICATION_NON_UNIQUE);
      g_value_unset (&value);

      g_assert_true (dex_generator_await_next (generator, &value, NULL));
      g_assert_true (G_VALUE_HOLDS (&value, G_TYPE_STRING));
      g_assert_cmpstr (g_value_get_string (&value), ==, "hello");
      g_value_unset (&value);

      g_assert_true (dex_generator_await_next (generator, &value, NULL));
      g_assert_true (G_VALUE_HOLDS (&value, G_TYPE_POINTER));
      g_assert_true (g_value_get_pointer (&value) == state);
      g_value_unset (&value);

      g_assert_true (dex_generator_await_next (generator, &value, NULL));
      g_assert_true (G_VALUE_HOLDS (&value, G_TYPE_OBJECT));
      g_assert_true (g_value_get_object (&value) == state->object);
      g_value_unset (&value);

      g_assert_true (dex_generator_await_next (generator, &value, NULL));
      g_assert_true (G_VALUE_HOLDS (&value, DEX_TYPE_FUTURE));
      g_assert_true (dex_value_get_object (&value) == DEX_OBJECT (state->deferred));
      g_value_unset (&value);

      g_assert_true (dex_generator_await_next (generator, &value, NULL));
      g_assert_true (G_VALUE_HOLDS (&value, G_TYPE_VARIANT));
      g_assert_true (g_value_get_variant (&value) == state->variant);
      g_value_unset (&value);

      g_assert_true (dex_generator_await_next (generator, &value, NULL));
      g_assert_true (G_VALUE_HOLDS (&value, G_TYPE_BYTES));
      g_assert_true (g_value_get_boxed (&value) == state->bytes);
      g_value_unset (&value);

      g_assert_cmpint (calls, ==, 0);
      g_assert_true (dex_generator_await_next_fd (generator, &fd, NULL));
      g_assert_cmpint (write (pipes[1], "x", 1), ==, 1);
      g_assert_cmpint (read (fd, &byte, 1), ==, 1);
      g_assert_cmpint (byte, ==, 'x');
      close (fd);
      close (pipes[1]);
      g_assert_false (dex_generator_await_next (generator, &value, NULL));
      g_assert_true (dex_await_boolean (dex_generator_close_after_drain (generator), NULL));
      dex_unref (generator);
      g_object_unref (state->object);
      dex_unref (state->deferred);
      g_variant_unref (state->variant);
      g_bytes_unref (state->bytes);
    }
}

static DexFuture *
infinite_producer (gpointer data)
{
  State *state = data;

  return dex_future_new_for_int (state->calls++);
}

static void
test_ordinary_errors (void)
{
  State state = { 0 };
  DexGenerator *generator = dex_generator_new (ordinary, &state, destroy_state);
  GValue value = G_VALUE_INIT;
  GError *error = NULL;
  int fd = 42;
  DexPromise *promise = dex_promise_new ();
  DexFuture *item;

  state.item = dex_future_new_reject (G_IO_ERROR,
                                      G_IO_ERROR_FAILED,
                                      "item failure");
  g_assert_false (dex_generator_await_next (generator, &value, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
  g_clear_error (&error);
  g_assert_cmpuint (G_VALUE_TYPE (&value), ==, 0);
  g_assert_false (dex_generator_is_exhausted (generator));
  state.item = dex_future_new_for_int (1);
  g_assert_false (dex_generator_await_next_fd (generator, &fd, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_TYPE_MISMATCH);
  g_clear_error (&error);
  g_assert_cmpint (fd, ==, -1);
  state.item = dex_ref (promise);
  item = dex_generator_next (generator);
  g_assert_true (dex_await_boolean (dex_generator_close_after_drain (generator), NULL));
  g_assert_true (dex_future_is_pending (item));
  dex_unref (generator);
  dex_promise_resolve_int (promise, 99);
  g_assert_cmpint (dex_await_int (item, NULL), ==, 99);
  dex_unref (promise);

  state.calls = 0;
  generator = dex_generator_new (infinite_producer, &state, NULL);
  for (int i = 0; i < 100; i++)
    {
      int number;

      g_assert_true (dex_generator_await_next_int (generator, &number, NULL));
      g_assert_cmpint (number, ==, i);
    }
  g_assert_false (dex_generator_is_exhausted (generator));
  dex_unref (generator);
  generator = dex_generator_new (ordinary, &state, NULL);
  g_assert_false (dex_generator_await_next_fd (generator, &fd, &error));
  g_assert_cmpint (fd, ==, -1);
  g_assert_no_error (error);
  dex_unref (generator);
}

static gpointer
pull_thread (gpointer data)
{
  return dex_generator_next (data);
}

static void
test_foreign_pull (void)
{
  for (guint kind = 0; kind < 2; kind++)
    {
      State state = { 0 };
      DexGenerator *generator = adapter (&state, kind);
      GThread *thread = g_thread_new ("generator-pull", pull_thread, generator);
      DexFuture *pull = g_thread_join (thread);

      g_assert_cmpint (state.calls, ==, 0);
      g_assert_cmpint (dex_await_int (pull, NULL), ==, 0);
      g_assert_true (dex_await_boolean (dex_generator_close_after_drain (generator), NULL));
      g_assert_true (state.thread == g_thread_self ());
      dex_unref (generator);
    }
}

typedef struct
{
  DexPromise *between;
  DexPromise *final;
  DexPromise *entered;
  int         destroyed;
} AwaitState;

static void
await_destroy (gpointer data)
{
  AwaitState *state = data;

  state->destroyed++;
}

static DexFuture *
await_fiber (gpointer data)
{
  AwaitState *state = data;
  GError *error = NULL;

  if (!dex_fiber_yield_int (1, &error))
    return dex_future_new_for_error (error);
  dex_promise_resolve_boolean (state->entered, TRUE);
  if (!dex_await (dex_ref (state->between), &error))
    return dex_future_new_for_error (error);

  if (!dex_fiber_yield_int (2, &error))
    return dex_future_new_for_error (error);
  return dex_ref (state->final);
}

static DexFuture *
await_coroutine (DexCoroutineContext *context,
                 gpointer             data)
{
  AwaitState *state = data;
  GError *error = NULL;

  DEX_COROUTINE_BEGIN (context);
  DEX_COROUTINE_YIELD_INT (1, &error);
  if (error != NULL)
    return dex_future_new_for_error (error);
  dex_promise_resolve_boolean (state->entered, TRUE);
  DEX_COROUTINE_SUSPEND (dex_ref (state->between), &error);
  if (error != NULL)
    return dex_future_new_for_error (error);
  DEX_COROUTINE_YIELD_INT (2, &error);
  if (error != NULL)
    return dex_future_new_for_error (error);
  return dex_ref (state->final);
  DEX_COROUTINE_END;
}

static void
test_await_between (void)
{
  for (guint kind = 0; kind < 2; kind++)
    {
      AwaitState state = { dex_promise_new (), dex_promise_new (), dex_promise_new (), 0 };
      DexGenerator *generator;
      DexFuture *pull;
      int number;

      if (kind)
        generator = dex_generator_new_coroutine (NULL, await_coroutine, &state, await_destroy);
      else
        generator = dex_generator_new_fiber (NULL, 0, await_fiber, &state, await_destroy);
      g_assert_true (dex_generator_await_next_int (generator, &number, NULL));
      g_assert_cmpint (number, ==, 1);
      g_assert_true (dex_future_is_pending (DEX_FUTURE (state.entered)));
      pull = dex_generator_next (generator);
      g_assert_true (dex_await_boolean (dex_ref (state.entered), NULL));
      g_assert_true (dex_future_is_pending (pull));
      dex_promise_resolve_boolean (state.between, TRUE);
      g_assert_cmpint (dex_await_int (pull, NULL), ==, 2);
      g_assert_false (dex_generator_is_exhausted (generator));
      pull = dex_generator_next (generator);
      g_assert_true (dex_fiber_yield (NULL));
      g_assert_true (dex_future_is_pending (pull));
      g_assert_false (dex_generator_is_exhausted (generator));
      dex_promise_resolve_boolean (state.final, TRUE);
      g_assert_true (dex_await (pull, NULL));
      g_assert_true (dex_generator_is_exhausted (generator));
      g_assert_true (dex_await_boolean (dex_generator_close_after_drain (generator), NULL));
      g_assert_cmpint (state.destroyed, ==, 1);
      dex_unref (state.between);
      dex_unref (state.final);
      dex_unref (state.entered);
      dex_unref (generator);
    }
}

static DexFuture *
empty_fiber (gpointer data)
{
  return GPOINTER_TO_INT (data) ? NULL : dex_future_new_true ();
}

static DexFuture *
empty_coroutine (DexCoroutineContext *context,
                 gpointer             data)
{
  return empty_fiber (data);
}

static void
test_empty_adapters (void)
{
  for (guint kind = 0; kind < 2; kind++)
    for (guint no_result = 0; no_result < 2; no_result++)
      {
        DexGenerator *generator;
        GError *error = NULL;
        int number;

        if (kind)
          generator = dex_generator_new_coroutine (NULL,
                                                   empty_coroutine,
                                                   GUINT_TO_POINTER (no_result),
                                                   NULL);
        else
          generator = dex_generator_new_fiber (NULL,
                                               0,
                                               empty_fiber,
                                               GUINT_TO_POINTER (no_result),
                                               NULL);
        g_assert_false (dex_generator_await_next_int (generator, &number, &error));
        if (no_result)
          {
            g_assert_error (error, DEX_ERROR, DEX_ERROR_FIBER_EXITED);
            g_clear_error (&error);
            g_assert_false (dex_generator_is_exhausted (generator));
          }
        else
          {
            g_assert_no_error (error);
            g_assert_true (dex_generator_is_exhausted (generator));
          }
        g_assert_true (dex_await_boolean (dex_generator_close_after_drain (generator), NULL));
        dex_unref (generator);
      }
}

int
main (int argc, char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);
  dex_test_add_func ("/generator/typed-yields", test_typed_yields);
  dex_test_add_func ("/generator/empty-adapters", test_empty_adapters);
  g_test_add_func ("/generator/ordinary", test_ordinary);
  g_test_add_func ("/generator/ordinary-errors", test_ordinary_errors);
  dex_test_add_func ("/generator/foreign-pull", test_foreign_pull);
  dex_test_add_func ("/generator/await-between", test_await_between);
  g_test_add_func ("/generator/recursive", test_recursive);
  g_test_add_func ("/generator/yield-context", test_yield_context);
  g_test_add_func ("/generator/owned-values", test_owned_values);
  dex_test_add_func ("/generator/adapters", test_adapters);
  dex_test_add_func ("/generator/reentry", test_reentry);
  dex_test_add_func ("/generator/cross-thread", test_cross_thread);
  dex_test_add_func ("/generator/interleaved", test_interleaved);
  g_test_add_func ("/generator/active-close", test_active_close);
  dex_test_add_func ("/generator/shutdown", test_shutdown);
  dex_test_add_func ("/generator/pending-cleanup", test_pending_cleanup);
  dex_test_add_func ("/generator/unrelated-fiber", test_unrelated_fiber);
  g_test_add_func ("/generator/types/boolean", test_type_boolean);
  g_test_add_func ("/generator/types/int", test_type_int);
  g_test_add_func ("/generator/types/uint", test_type_uint);
  g_test_add_func ("/generator/types/int64", test_type_int64);
  g_test_add_func ("/generator/types/uint64", test_type_uint64);
  g_test_add_func ("/generator/types/float", test_type_float);
  g_test_add_func ("/generator/types/double", test_type_double);
  g_test_add_func ("/generator/types/enum", test_type_enum);
  g_test_add_func ("/generator/types/flags", test_type_flags);
  g_test_add_func ("/generator/types/string", test_type_string);
  g_test_add_func ("/generator/types/variant", test_type_variant);
  g_test_add_func ("/generator/types/boxed", test_type_boxed);
  g_test_add_func ("/generator/types/pointer", test_type_pointer);
  g_test_add_func ("/generator/types/object", test_type_object);
  g_test_add_func ("/generator/types/future", test_type_future);

  return g_test_run ();
}
