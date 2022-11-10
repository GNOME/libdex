/* test-future.c
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

#include <gio/gio.h>

#include "dex-future-private.h"

#define ASSERT_STATUS(f,status) g_assert_cmpint(status, ==, dex_future_get_status(DEX_FUTURE(f)))

typedef struct
{
  guint catch;
  guint destroy;
  guint finally;
  guint then;
} TestInfo;

static DexFuture *
catch_cb (DexFuture *future,
          gpointer   user_data)
{
  TestInfo *info = user_data;
  g_atomic_int_inc (&info->catch);
  return DEX_FUTURE (dex_promise_new_for_string ("123"));
}

static DexFuture *
then_cb (DexFuture *future,
         gpointer   user_data)
{
  TestInfo *info = user_data;
  const GValue *value;
  GError *error = NULL;

  g_atomic_int_inc (&info->then);

  value = dex_future_get_value (future, &error);
  g_assert_no_error (error);
  g_assert_nonnull (value);
  g_assert_true (G_VALUE_HOLDS_STRING (value));
  g_assert_cmpstr (g_value_get_string (value), ==, "123");

  return DEX_FUTURE (dex_promise_new_for_int (123));
}

static DexFuture *
finally_cb (DexFuture *future,
            gpointer   user_data)
{
  TestInfo *info = user_data;
  const GValue *value;
  GError *error = NULL;

  g_atomic_int_inc (&info->finally);

  value = dex_future_get_value (future, &error);
  g_assert_no_error (error);
  g_assert_nonnull (value);
  g_assert_true (G_VALUE_HOLDS_INT (value));
  g_assert_cmpint (g_value_get_int (value), ==, 123);

  return NULL;
}

static void
destroy_cb (gpointer data)
{
  TestInfo *info = data;
  g_atomic_int_inc (&info->destroy);
}

static void
test_future_then (void)
{
  DexCancellable *cancellable;
  const GValue *resolved;
  DexFuture *future;
  GError *error = NULL;
  TestInfo info = {0};

  cancellable = dex_cancellable_new ();
  g_assert_cmpint (dex_future_get_status (DEX_FUTURE (cancellable)), ==, DEX_FUTURE_STATUS_PENDING);

  dex_cancellable_cancel (cancellable);
  g_assert_cmpint (dex_future_get_status (DEX_FUTURE (cancellable)), ==, DEX_FUTURE_STATUS_REJECTED);

  future = dex_future_catch (dex_ref (cancellable), catch_cb, &info, destroy_cb);
  g_assert_cmpint (dex_future_get_status (future), ==, DEX_FUTURE_STATUS_RESOLVED);

  future = dex_future_then (future, then_cb, &info, destroy_cb);
  g_assert_cmpint (dex_future_get_status (future), ==, DEX_FUTURE_STATUS_RESOLVED);

  future = dex_future_finally (future, finally_cb, &info, destroy_cb);
  g_assert_cmpint (dex_future_get_status (future), ==, DEX_FUTURE_STATUS_RESOLVED);

  resolved = dex_future_get_value (future, &error);
  g_assert_no_error (error);
  g_assert_nonnull (resolved);
  g_assert_true (G_VALUE_HOLDS_INT (resolved));
  g_assert_cmpint (g_value_get_int (resolved), ==, 123);

  dex_unref (future);
  dex_unref (cancellable);

  g_assert_cmpint (info.catch, ==, 1);
  g_assert_cmpint (info.finally, ==, 1);
  g_assert_cmpint (info.then, ==, 1);
  g_assert_cmpint (info.destroy, ==, 3);
}

static void
test_cancellable_cancel (void)
{
  DexCancellable *future = dex_cancellable_new ();
  g_assert_cmpint (dex_future_get_status (DEX_FUTURE (future)), ==, DEX_FUTURE_STATUS_PENDING);
  dex_cancellable_cancel (future);
  g_assert_cmpint (dex_future_get_status (DEX_FUTURE (future)), ==, DEX_FUTURE_STATUS_REJECTED);
  dex_unref (future);
}

static DexFuture *
on_timed_out (DexFuture *future,
              gpointer   user_data)
{
  g_main_loop_quit (user_data);
  return NULL;
}

static void
test_timeout (void)
{
  GMainLoop *main_loop = g_main_loop_new (NULL, FALSE);
  DexTimeout *timeout = dex_timeout_new_deadline (g_get_monotonic_time ());
  DexFuture *future = dex_future_catch (dex_ref (timeout), on_timed_out, main_loop, NULL);

  g_assert_cmpint (dex_future_get_status (DEX_FUTURE (timeout)), ==, DEX_FUTURE_STATUS_PENDING);
  g_assert_cmpint (dex_future_get_status (future), ==, DEX_FUTURE_STATUS_PENDING);

  g_main_loop_run (main_loop);

  g_assert_cmpint (dex_future_get_status (DEX_FUTURE (timeout)), ==, DEX_FUTURE_STATUS_REJECTED);
  g_assert_cmpint (dex_future_get_status (future), ==, DEX_FUTURE_STATUS_REJECTED);

  dex_clear (&future);
  dex_clear (&timeout);

  g_main_loop_unref (main_loop);
}

static void
test_promise_type (void)
{
  g_assert_true (dex_promise_get_type() != G_TYPE_INVALID);
  g_assert_true (G_TYPE_IS_INSTANTIATABLE (dex_promise_get_type()));
  g_assert_false (G_TYPE_IS_ABSTRACT (dex_promise_get_type()));
  g_assert_true (G_TYPE_IS_FINAL (dex_promise_get_type()));
}

static void
test_promise_autoptr (void)
{
  G_GNUC_UNUSED g_autoptr(DexPromise) promise = NULL;
}

static void
test_promise_new (void)
{
  DexPromise *promise;

  promise = dex_promise_new ();
  g_assert_cmpint (dex_future_get_status (DEX_FUTURE (promise)), ==, DEX_FUTURE_STATUS_PENDING);
  g_assert_nonnull (promise);
  g_assert_true (DEX_IS_FUTURE (promise));
  dex_clear (&promise);

  promise = dex_promise_new_for_string ("123");
  g_assert_true (DEX_IS_PROMISE (promise));
  g_assert_cmpint (dex_future_get_status (DEX_FUTURE (promise)), ==, DEX_FUTURE_STATUS_RESOLVED);
  dex_clear (&promise);

  promise = dex_promise_new_for_int (123);
  g_assert_true (DEX_IS_PROMISE (promise));
  g_assert_cmpint (dex_future_get_status (DEX_FUTURE (promise)), ==, DEX_FUTURE_STATUS_RESOLVED);
  dex_clear (&promise);

  promise = dex_promise_new_for_boolean (TRUE);
  g_assert_true (DEX_IS_PROMISE (promise));
  g_assert_cmpint (dex_future_get_status (DEX_FUTURE (promise)), ==, DEX_FUTURE_STATUS_RESOLVED);
  dex_clear (&promise);

  promise = dex_promise_new_for_error (g_error_new_literal (G_IO_ERROR, G_IO_ERROR_PENDING, "pending"));
  g_assert_true (DEX_IS_PROMISE (promise));
  g_assert_cmpint (dex_future_get_status (DEX_FUTURE (promise)), ==, DEX_FUTURE_STATUS_REJECTED);
  dex_clear (&promise);
}

static void
test_promise_resolve (void)
{
  DexPromise *promise = dex_promise_new ();
  GValue value = G_VALUE_INIT;
  const GValue *resolved;
  GError *error = NULL;

  g_assert_cmpint (dex_future_get_status (DEX_FUTURE (promise)), ==, DEX_FUTURE_STATUS_PENDING);

  g_value_init (&value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&value, TRUE);
  dex_promise_resolve (promise, &value);

  g_assert_cmpint (dex_future_get_status (DEX_FUTURE (promise)), ==, DEX_FUTURE_STATUS_RESOLVED);

  resolved = dex_future_get_value (DEX_FUTURE (promise), &error);
  g_assert_no_error (error);
  g_assert_cmpint (G_VALUE_TYPE (resolved), ==, G_VALUE_TYPE (&value));
  g_assert_cmpint (g_value_get_boolean (resolved), ==, g_value_get_boolean (&value));

  g_value_unset (&value);
  dex_unref (promise);
}

#define ASYNC_TEST(T, NAME, propagate, gvalue, res, cmp) \
typedef struct G_PASTE (_Test, T) G_PASTE (Test, T); \
static void \
G_PASTE (test_async_, T) (G_PASTE (Test, T)   *instance, \
                          GCancellable        *cancellable, \
                          GAsyncReadyCallback  callback, \
                          gpointer             user_data) \
{ \
  GTask *task = g_task_new (instance, cancellable, callback, user_data); \
  g_assert_true (G_IS_OBJECT (instance)); \
  g_assert_true (!cancellable || G_IS_CANCELLABLE (cancellable)); \
  g_assert_true (callback != NULL); \
  G_PASTE (g_task_return_, propagate)(task, res); \
  g_object_unref (task); \
} \
static T \
G_PASTE (test_finish_, T)(G_PASTE (Test, T) *instance, GAsyncResult *result, GError **error) \
{ \
  g_assert_true (G_IS_TASK (result)); \
  g_assert_true (G_IS_OBJECT (instance)); \
  return G_PASTE (g_task_propagate_, propagate) (G_TASK (result), error); \
} \
static DexFuture * \
G_PASTE (test_complete_, T) (DexFuture *future, gpointer user_data) \
{ \
  GMainLoop *main_loop = user_data; \
  GError *error = NULL; \
  const GValue *value = dex_future_get_value (future, &error); \
  g_assert_cmpint (dex_future_get_status (future), ==, DEX_FUTURE_STATUS_RESOLVED); \
  g_assert_no_error (error); \
  g_assert_true (G_PASTE (G_VALUE_HOLDS_, NAME)(value)); \
  G_PASTE (g_assert_, cmp) (G_PASTE (g_value_get_, gvalue) (value), ==, res); \
  g_main_loop_quit (main_loop); \
  return NULL; \
} \
static void \
G_PASTE (test_async_pair_, T) (void) \
{ \
  GMainLoop *main_loop = g_main_loop_new (NULL, FALSE); \
  GObject *object = G_OBJECT (g_menu_new ()); \
  DexFuture *future = dex_async_pair_new (object, \
                                          &G_PASTE (DEX_ASYNC_PAIR_INFO_, NAME) (G_PASTE (test_async_, T), \
                                                                                 G_PASTE (test_finish_, T))); \
  future = dex_future_finally (future, G_PASTE (test_complete_, T), main_loop, NULL); \
  g_main_loop_run (main_loop); \
  dex_unref (future); \
  g_clear_object (&object); \
  g_main_loop_unref (main_loop); \
}

ASYNC_TEST (gboolean, BOOLEAN, boolean, boolean, TRUE, cmpint)
ASYNC_TEST (int, INT, int, int, TRUE, cmpint)
ASYNC_TEST (uint, UINT, int, uint, TRUE, cmpint)

#define ASYNC_TEST_PTR(T, NAME, propagate, gvalue, res, cmp, copyfunc, freefunc) \
typedef struct G_PASTE (_Test, T) G_PASTE (Test, T); \
static void \
G_PASTE (test_async_, T) (G_PASTE (Test, T)   *instance, \
                          GCancellable        *cancellable, \
                          GAsyncReadyCallback  callback, \
                          gpointer             user_data) \
{ \
  GTask *task = g_task_new (instance, cancellable, callback, user_data); \
  g_assert_true (G_IS_OBJECT (instance)); \
  g_assert_true (!cancellable || G_IS_CANCELLABLE (cancellable)); \
  g_assert_true (callback != NULL); \
  G_PASTE (g_task_return_, propagate)(task, copyfunc (res), (GDestroyNotify)freefunc); \
  g_object_unref (task); \
} \
static T * \
G_PASTE (test_finish_, T)(G_PASTE (Test, T) *instance, GAsyncResult *result, GError **error) \
{ \
  g_assert_true (G_IS_TASK (result)); \
  g_assert_true (G_IS_OBJECT (instance)); \
  return G_PASTE (g_task_propagate_, propagate) (G_TASK (result), error); \
} \
static DexFuture * \
G_PASTE (test_complete_, T) (DexFuture *future, gpointer user_data) \
{ \
  GMainLoop *main_loop = user_data; \
  GError *error = NULL; \
  const GValue *value = dex_future_get_value (future, &error); \
  const T *ret = G_PASTE (g_value_get_, gvalue) (value); \
  g_assert_cmpint (dex_future_get_status (future), ==, DEX_FUTURE_STATUS_RESOLVED); \
  g_assert_no_error (error); \
  g_assert_true (G_PASTE (G_VALUE_HOLDS_, NAME)(value)); \
  G_PASTE (g_assert_, cmp) (ret, ==, res); \
  g_main_loop_quit (main_loop); \
  return NULL; \
} \
static void \
G_PASTE (test_async_pair_, T) (void) \
{ \
  GMainLoop *main_loop = g_main_loop_new (NULL, FALSE); \
  GObject *object = G_OBJECT (g_menu_new ()); \
  DexFuture *future = dex_async_pair_new (object, \
                                          &G_PASTE (DEX_ASYNC_PAIR_INFO_, NAME) (G_PASTE (test_async_, T), \
                                                                                 G_PASTE (test_finish_, T))); \
  future = dex_future_finally (future, G_PASTE (test_complete_, T), main_loop, NULL); \
  g_main_loop_run (main_loop); \
  dex_unref (future); \
  g_clear_object (&object); \
  g_main_loop_unref (main_loop); \
}

ASYNC_TEST_PTR (char, STRING, pointer, string, "string-test", cmpstr, g_strdup, g_free)

typedef struct _AsyncObject AsyncObject;

static void
test_object_async (AsyncObject         *instance,
                   GCancellable        *cancellable,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data)
{
  GTask *task = g_task_new (instance, cancellable, callback, user_data);
  g_assert_true (G_IS_OBJECT (instance));
  g_assert_true (!cancellable || G_IS_CANCELLABLE (cancellable));
  g_assert_true (callback != NULL);
  g_task_return_pointer (task, g_object_ref (instance), g_object_unref);
  g_object_unref (task);
}

static GObject *
test_object_finish (AsyncObject   *instance,
                    GAsyncResult  *result,
                    GError       **error)
{
  g_assert_true (G_IS_TASK (result));
  g_assert_true (G_IS_OBJECT (instance));
  return g_task_propagate_pointer (G_TASK (result), error);
}

static DexFuture *
test_object_complete (DexFuture *future,
                      gpointer   user_data)
{
  GMainLoop *main_loop = user_data;
  GError *error = NULL;
  const GValue *value = dex_future_get_value (future, &error);
  g_assert_cmpint (dex_future_get_status (future), ==, DEX_FUTURE_STATUS_RESOLVED);
  g_assert_no_error (error);
  g_assert_true (G_VALUE_HOLDS_OBJECT (value));
  g_assert_nonnull (g_value_get_object (value));
  g_main_loop_quit (main_loop);
  return NULL;
}

static void
test_async_pair_object (void)
{
  GMainLoop *main_loop = g_main_loop_new (NULL, FALSE);
  GObject *object = G_OBJECT (g_menu_new ());
  DexFuture *future = dex_async_pair_new (object,
                                          &DEX_ASYNC_PAIR_INFO_OBJECT (test_object_async,
                                                                       test_object_finish));
  future = dex_future_finally (future, test_object_complete, main_loop, NULL);
  g_main_loop_run (main_loop);
  dex_unref (future);
  g_clear_object (&object);
  g_main_loop_unref (main_loop);
}

static void
test_future_all (void)
{
  DexCancellable *cancel1 = dex_cancellable_new ();
  DexCancellable *cancel2 = dex_cancellable_new ();
  DexCancellable *cancel3 = dex_cancellable_new ();
  const GValue *value;
  DexFuture *future;
  GError *error = NULL;

  future = dex_future_all (dex_ref (cancel1), dex_ref (cancel2), dex_ref (cancel3), NULL);
  ASSERT_STATUS (cancel1, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (cancel2, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (cancel3, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (future, DEX_FUTURE_STATUS_PENDING);

  dex_cancellable_cancel (cancel1);
  ASSERT_STATUS (cancel1, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (cancel2, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (cancel3, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (future, DEX_FUTURE_STATUS_PENDING);

  dex_cancellable_cancel (cancel2);
  ASSERT_STATUS (cancel1, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (cancel2, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (cancel3, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (future, DEX_FUTURE_STATUS_PENDING);

  dex_cancellable_cancel (cancel3);
  ASSERT_STATUS (cancel1, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (cancel2, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (cancel3, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (future, DEX_FUTURE_STATUS_REJECTED);
  value = dex_future_get_value (future, &error);
  g_assert_null (value);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
  g_clear_error (&error);

  g_assert_true (DEX_IS_FUTURE_SET (future));
  g_assert_cmpint (dex_future_set_get_size (DEX_FUTURE_SET (future)), ==, 3);

  for (guint i = 0; i < dex_future_set_get_size (DEX_FUTURE_SET (future)); i++)
    {
      DexFuture *dep = dex_future_set_get_future (DEX_FUTURE_SET (future), i);
      value = dex_future_get_value (dep, &error);
      g_assert_null (value);
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
      g_clear_error (&error);
    }

  dex_clear (&cancel1);
  dex_clear (&cancel2);
  dex_clear (&cancel3);
  dex_clear (&future);
}

static void
test_future_all_race (void)
{
  DexCancellable *cancel1 = dex_cancellable_new ();
  DexCancellable *cancel2 = dex_cancellable_new ();
  DexCancellable *cancel3 = dex_cancellable_new ();
  const GValue *value;
  DexFuture *future;
  GError *error = NULL;

  future = dex_future_all_race (dex_ref (cancel1), dex_ref (cancel2), dex_ref (cancel3), NULL);
  ASSERT_STATUS (cancel1, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (cancel2, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (cancel3, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (future, DEX_FUTURE_STATUS_PENDING);

  dex_cancellable_cancel (cancel1);
  ASSERT_STATUS (cancel1, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (cancel2, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (cancel3, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (future, DEX_FUTURE_STATUS_REJECTED);

  dex_cancellable_cancel (cancel2);
  ASSERT_STATUS (cancel1, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (cancel2, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (cancel3, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (future, DEX_FUTURE_STATUS_REJECTED);

  dex_cancellable_cancel (cancel3);
  ASSERT_STATUS (cancel1, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (cancel2, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (cancel3, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (future, DEX_FUTURE_STATUS_REJECTED);
  value = dex_future_get_value (future, &error);
  g_assert_null (value);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_clear_error (&error);

  g_assert_true (DEX_IS_FUTURE_SET (future));

  g_assert_cmpint (dex_future_set_get_size (DEX_FUTURE_SET (future)), ==, 3);
  for (guint i = 0; i < dex_future_set_get_size (DEX_FUTURE_SET (future)); i++)
    {
      DexFuture *dep = dex_future_set_get_future (DEX_FUTURE_SET (future), i);
      value = dex_future_get_value (dep, &error);
      g_assert_null (value);
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
      g_clear_error (&error);
    }

  dex_clear (&cancel1);
  dex_clear (&cancel2);
  dex_clear (&cancel3);
  dex_clear (&future);
}

static void
test_future_any (void)
{
  DexCancellable *cancel1 = dex_cancellable_new ();
  DexCancellable *cancel2 = dex_cancellable_new ();
  DexCancellable *cancel3 = dex_cancellable_new ();
  const GValue *value;
  DexFuture *future;
  GError *error = NULL;

  future = dex_future_any (dex_ref (cancel1), dex_ref (cancel2), dex_ref (cancel3), NULL);
  ASSERT_STATUS (cancel1, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (cancel2, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (cancel3, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (future, DEX_FUTURE_STATUS_PENDING);

  dex_cancellable_cancel (cancel1);
  ASSERT_STATUS (cancel1, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (cancel2, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (cancel3, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (future, DEX_FUTURE_STATUS_PENDING);

  dex_cancellable_cancel (cancel2);
  ASSERT_STATUS (cancel1, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (cancel2, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (cancel3, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (future, DEX_FUTURE_STATUS_PENDING);

  dex_cancellable_cancel (cancel3);
  ASSERT_STATUS (cancel1, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (cancel2, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (cancel3, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (future, DEX_FUTURE_STATUS_REJECTED);
  value = dex_future_get_value (future, &error);
  g_assert_null (value);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
  g_clear_error (&error);

  g_assert_true (DEX_IS_FUTURE_SET (future));

  g_assert_cmpint (dex_future_set_get_size (DEX_FUTURE_SET (future)), ==, 3);
  for (guint i = 0; i < dex_future_set_get_size (DEX_FUTURE_SET (future)); i++)
    {
      DexFuture *dep = dex_future_set_get_future (DEX_FUTURE_SET (future), i);
      value = dex_future_get_value (dep, &error);
      g_assert_null (value);
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
      g_clear_error (&error);
    }

  dex_clear (&cancel1);
  dex_clear (&cancel2);
  dex_clear (&cancel3);
  dex_clear (&future);
}

static void
test_future_any_race (void)
{
  DexCancellable *cancel1 = dex_cancellable_new ();
  DexCancellable *cancel2 = dex_cancellable_new ();
  DexCancellable *cancel3 = dex_cancellable_new ();
  const GValue *value;
  DexFuture *future;
  GError *error = NULL;

  future = dex_future_any_race (dex_ref (cancel1), dex_ref (cancel2), dex_ref (cancel3), NULL);
  ASSERT_STATUS (cancel1, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (cancel2, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (cancel3, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (future, DEX_FUTURE_STATUS_PENDING);

  dex_cancellable_cancel (cancel1);
  ASSERT_STATUS (cancel1, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (cancel2, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (cancel3, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (future, DEX_FUTURE_STATUS_REJECTED);

  dex_cancellable_cancel (cancel2);
  ASSERT_STATUS (cancel1, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (cancel2, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (cancel3, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (future, DEX_FUTURE_STATUS_REJECTED);

  dex_cancellable_cancel (cancel3);
  ASSERT_STATUS (cancel1, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (cancel2, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (cancel3, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (future, DEX_FUTURE_STATUS_REJECTED);
  value = dex_future_get_value (future, &error);
  g_assert_null (value);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_clear_error (&error);

  g_assert_true (DEX_IS_FUTURE_SET (future));

  g_assert_cmpint (dex_future_set_get_size (DEX_FUTURE_SET (future)), ==, 3);
  for (guint i = 0; i < dex_future_set_get_size (DEX_FUTURE_SET (future)); i++)
    {
      DexFuture *dep = dex_future_set_get_future (DEX_FUTURE_SET (future), i);
      value = dex_future_get_value (dep, &error);
      g_assert_null (value);
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
      g_clear_error (&error);
    }

  dex_clear (&cancel1);
  dex_clear (&cancel2);
  dex_clear (&cancel3);
  dex_clear (&future);
}

int
main (int   argc,
      char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Dex/TestSuite/Block/then", test_future_then);
  g_test_add_func ("/Dex/TestSuite/Cancellable/cancel", test_cancellable_cancel);
  g_test_add_func ("/Dex/TestSuite/Promise/type", test_promise_type);
  g_test_add_func ("/Dex/TestSuite/Promise/autoptr", test_promise_autoptr);
  g_test_add_func ("/Dex/TestSuite/Promise/new", test_promise_new);
  g_test_add_func ("/Dex/TestSuite/Promise/resolve", test_promise_resolve);
  g_test_add_func ("/Dex/TestSuite/Timeout/timed-out", test_timeout);
  g_test_add_func ("/Dex/TestSuite/AsyncPair/boolean", test_async_pair_gboolean);
  g_test_add_func ("/Dex/TestSuite/AsyncPair/int", test_async_pair_int);
  g_test_add_func ("/Dex/TestSuite/AsyncPair/uint", test_async_pair_uint);
  g_test_add_func ("/Dex/TestSuite/AsyncPair/string", test_async_pair_char);
  g_test_add_func ("/Dex/TestSuite/AsyncPair/object", test_async_pair_object);
  g_test_add_func ("/Dex/TestSuite/Future/all", test_future_all);
  g_test_add_func ("/Dex/TestSuite/Future/all_race", test_future_all_race);
  g_test_add_func ("/Dex/TestSuite/Future/any", test_future_any);
  g_test_add_func ("/Dex/TestSuite/Future/any_race", test_future_any_race);
  return g_test_run ();
}
