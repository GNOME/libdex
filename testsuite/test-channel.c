/*
 * test-channel.c
 *
 * Copyright 2022 Christian Hergert <christian@sourceandstack.com>
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

#include "config.h"

#include <libdex.h>

#define ASSERT_STATUS(f,status) g_assert_cmpint(status, ==, dex_future_get_status(DEX_FUTURE(f)))

#define ASSERT_CMP(future, kind, get, op, v) \
  G_STMT_START { \
    GError *error = NULL; \
    const GValue *value = dex_future_get_value (DEX_FUTURE (future), &error); \
    g_assert_no_error (error); \
    g_assert_nonnull (value); \
    G_PASTE (g_assert_cmp, kind) (G_PASTE (g_value_get_, get) (value), op, v); \
  } G_STMT_END
#define ASSERT_CMPINT(future, op, value) ASSERT_CMP(future, int, int, op, value)
#define ASSERT_CMPUINT(future, op, value) ASSERT_CMP(future, int, uint, op, value)

static void
test_channel_basic (void)
{
  DexChannel *channel;
  DexFuture *value1 = NULL;
  DexFuture *value2 = NULL;
  DexFuture *value3 = NULL;
  DexFuture *send1 = NULL;
  DexFuture *send2 = NULL;
  DexFuture *send3 = NULL;
  DexFuture *recv1 = NULL;
  DexFuture *recv2 = NULL;
  DexFuture *recv3 = NULL;

  channel = dex_channel_new (2);
  g_assert_true (dex_channel_can_send (channel));
  g_assert_true (dex_channel_can_receive (channel));

  value1 = dex_future_new_for_int (1);
  value2 = dex_future_new_for_int (2);
  value3 = dex_future_new_for_int (3);
  ASSERT_CMPINT (value1, ==, 1);
  ASSERT_CMPINT (value2, ==, 2);
  ASSERT_CMPINT (value3, ==, 3);

  send1 = dex_channel_send (channel, dex_ref (value1));
  g_assert_true ((gpointer)send1 != (gpointer)value1);
  g_assert_true (dex_channel_can_send (channel));
  g_assert_true (dex_channel_can_receive (channel));
  ASSERT_STATUS (send1, DEX_FUTURE_STATUS_RESOLVED);
  ASSERT_CMPUINT (send1, ==, 1);

  send2 = dex_channel_send (channel, dex_ref (value2));
  g_assert_true (dex_channel_can_send (channel));
  g_assert_true (dex_channel_can_receive (channel));
  ASSERT_STATUS (send2, DEX_FUTURE_STATUS_RESOLVED);
  ASSERT_CMPUINT (send2, ==, 2);

  send3 = dex_channel_send (channel, dex_ref (value3));
  g_assert_true (dex_channel_can_send (channel));
  g_assert_true (dex_channel_can_receive (channel));
  ASSERT_STATUS (send3, DEX_FUTURE_STATUS_PENDING);

  dex_channel_close_send (channel);
  g_assert_false (dex_channel_can_send (channel));
  g_assert_true (dex_channel_can_receive (channel));
  ASSERT_STATUS (send3, DEX_FUTURE_STATUS_PENDING);

  recv1 = dex_channel_receive (channel);
  ASSERT_STATUS (send3, DEX_FUTURE_STATUS_RESOLVED);
  ASSERT_STATUS (recv1, DEX_FUTURE_STATUS_RESOLVED);
  ASSERT_CMPUINT (send3, ==, 2);
  ASSERT_CMPINT (recv1, ==, 1);

  recv2 = dex_channel_receive (channel);
  ASSERT_STATUS (recv2, DEX_FUTURE_STATUS_RESOLVED);
  ASSERT_CMPINT (recv2, ==, 2);

  dex_channel_close_receive (channel);
  g_assert_false (dex_channel_can_send (channel));
  g_assert_false (dex_channel_can_receive (channel));

  recv3 = dex_channel_receive (channel);
  ASSERT_STATUS (recv3, DEX_FUTURE_STATUS_REJECTED);

  dex_clear (&value1);
  dex_clear (&value2);
  dex_clear (&value3);
  dex_clear (&send1);
  dex_clear (&send2);
  dex_clear (&send3);
  dex_clear (&recv1);
  dex_clear (&recv2);
  dex_clear (&recv3);
  dex_clear (&channel);
}

static void
test_channel_recv_first (void)
{
  DexChannel *channel = dex_channel_new (2);
  DexFuture *recv1 = dex_channel_receive (channel);
  DexFuture *recv2 = dex_channel_receive (channel);
  DexFuture *recv3 = dex_channel_receive (channel);
  DexFuture *recv4;
  DexFuture *value1 = dex_future_new_for_int (123);
  DexFuture *send1;

  ASSERT_STATUS (recv1, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (recv2, DEX_FUTURE_STATUS_PENDING);

  send1 = dex_channel_send (channel, dex_ref (value1));
  ASSERT_STATUS (send1, DEX_FUTURE_STATUS_RESOLVED);
  ASSERT_STATUS (recv1, DEX_FUTURE_STATUS_RESOLVED);
  ASSERT_STATUS (recv2, DEX_FUTURE_STATUS_PENDING);

  dex_channel_close_send (channel);
  ASSERT_STATUS (recv2, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (recv3, DEX_FUTURE_STATUS_REJECTED);

  recv4 = dex_channel_receive (channel);
  ASSERT_STATUS (recv4, DEX_FUTURE_STATUS_REJECTED);

  dex_clear (&channel);
  dex_clear (&recv1);
  dex_clear (&recv2);
  dex_clear (&recv3);
  dex_clear (&recv4);
  dex_clear (&value1);
  dex_clear (&send1);
}

static void
test_channel_receive_all_with_blocked_sender (void)
{
  DexChannel *channel = dex_channel_new (2);
  DexFuture *value1 = dex_future_new_for_int (1);
  DexFuture *value2 = dex_future_new_for_int (2);
  DexFuture *value3 = dex_future_new_for_int (3);
  DexFuture *send1;
  DexFuture *send2;
  DexFuture *send3;
  DexFuture *all;
  DexFuture *recv;

  send1 = dex_channel_send (channel, dex_ref (value1));
  send2 = dex_channel_send (channel, dex_ref (value2));
  send3 = dex_channel_send (channel, dex_ref (value3));

  ASSERT_STATUS (send1, DEX_FUTURE_STATUS_RESOLVED);
  ASSERT_STATUS (send2, DEX_FUTURE_STATUS_RESOLVED);
  ASSERT_STATUS (send3, DEX_FUTURE_STATUS_PENDING);

  all = dex_channel_receive_all (channel);

  ASSERT_STATUS (all, DEX_FUTURE_STATUS_RESOLVED);
  ASSERT_STATUS (send3, DEX_FUTURE_STATUS_RESOLVED);
  ASSERT_CMPUINT (send3, ==, 1);
  g_assert_cmpuint (dex_future_set_get_size (DEX_FUTURE_SET (all)), ==, 2);
  ASSERT_CMPINT (dex_future_set_get_future_at (DEX_FUTURE_SET (all), 0), ==, 1);
  ASSERT_CMPINT (dex_future_set_get_future_at (DEX_FUTURE_SET (all), 1), ==, 2);

  recv = dex_channel_receive (channel);
  ASSERT_STATUS (recv, DEX_FUTURE_STATUS_RESOLVED);
  ASSERT_CMPINT (recv, ==, 3);

  dex_clear (&channel);
  dex_clear (&value1);
  dex_clear (&value2);
  dex_clear (&value3);
  dex_clear (&send1);
  dex_clear (&send2);
  dex_clear (&send3);
  dex_clear (&all);
  dex_clear (&recv);
}

static void
test_channel_receive_with_cancellation (void)
{
  g_autoptr(DexChannel) channel = NULL;
  g_autoptr(DexCancellable) closed = NULL;
  g_autoptr(DexPromise) payload = NULL;
  g_autoptr(DexFuture) completed = NULL;
  g_autoptr(DexFuture) paired = NULL;
  g_autoptr(DexFuture) waiting = NULL;
  g_autoptr(DexFuture) paired_result = NULL;
  g_autoptr(DexFuture) waiting_result = NULL;
  g_autoptr(DexFuture) send = NULL;
  g_autoptr(GError) receive_error = NULL;

  channel = dex_channel_new (0);
  closed = dex_cancellable_new ();
  payload = dex_promise_new ();

  /* A delivered message must not cancel the shared connection lifetime. */
  completed = dex_future_first (dex_channel_receive (channel), dex_ref (closed), NULL);
  send = dex_channel_send (channel, dex_future_new_for_int (42));
  ASSERT_CMPINT (completed, ==, 42);
  ASSERT_STATUS (closed, DEX_FUTURE_STATUS_PENDING);
  dex_clear (&send);

  paired = dex_channel_receive (channel);
  waiting = dex_channel_receive (channel);
  paired_result = dex_future_first (dex_ref (paired), dex_ref (closed), NULL);
  waiting_result = dex_future_first (dex_ref (waiting), dex_ref (closed), NULL);

  /* Handoff removes the first receiver from the channel before its payload completes. */
  send = dex_channel_send (channel, dex_ref (payload));
  ASSERT_STATUS (send, DEX_FUTURE_STATUS_RESOLVED);
  ASSERT_STATUS (paired, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (waiting, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (paired_result, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (waiting_result, DEX_FUTURE_STATUS_PENDING);

  /* Simulate connection closure using the same cancellation future for both readers. */
  dex_cancellable_cancel (closed);
  dex_channel_close_receive (channel);

  ASSERT_STATUS (paired_result, DEX_FUTURE_STATUS_REJECTED);
  g_assert_null (dex_future_get_value (paired_result, &receive_error));
  g_assert_error (receive_error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_clear_error (&receive_error);

  ASSERT_STATUS (waiting_result, DEX_FUTURE_STATUS_REJECTED);
  g_assert_null (dex_future_get_value (waiting_result, &receive_error));
  g_assert_error (receive_error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_clear_error (&receive_error);

  /* Channel closure reaches only the receiver that has not been paired yet. */
  ASSERT_STATUS (paired, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (payload, DEX_FUTURE_STATUS_PENDING);
  ASSERT_STATUS (waiting, DEX_FUTURE_STATUS_REJECTED);
  g_assert_null (dex_future_get_value (waiting, &receive_error));
  g_assert_error (receive_error, DEX_ERROR, DEX_ERROR_CHANNEL_CLOSED);
  g_clear_error (&receive_error);

  /* Late completion cannot replace cancellation or invalidate an earlier delivery. */
  dex_promise_resolve_int (payload, 123);
  ASSERT_CMPINT (paired, ==, 123);
  ASSERT_STATUS (paired_result, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_STATUS (waiting_result, DEX_FUTURE_STATUS_REJECTED);
  ASSERT_CMPINT (completed, ==, 42);
}

static void
channel_cancelled_cb (GCancellable   *cancellable,
                      DexCancellable *closed)
{
  dex_cancellable_cancel (closed);
  ASSERT_STATUS (closed, DEX_FUTURE_STATUS_REJECTED);
}

static gboolean
channel_cancel_idle_cb (gpointer user_data)
{
  g_cancellable_cancel (user_data);

  return G_SOURCE_REMOVE;
}

static void
test_channel_await_cancellation_from_signal (void)
{
  for (guint paired = 0; paired < 2; paired++)
    {
      g_autoptr(DexChannel) channel = NULL;
      g_autoptr(DexCancellable) closed = NULL;
      g_autoptr(DexPromise) payload = NULL;
      g_autoptr(DexFuture) send = NULL;
      g_autoptr(DexFuture) race = NULL;
      g_autoptr(GCancellable) cancellable = NULL;
      g_autoptr(GSource) source = NULL;
      g_autoptr(GBytes) message = NULL;
      g_autoptr(GError) error = NULL;
      gulong handler;

      channel = dex_channel_new (0);
      closed = dex_cancellable_new ();
      payload = dex_promise_new ();
      cancellable = g_cancellable_new ();
      source = g_idle_source_new ();

      if (paired)
        send = dex_channel_send (channel, dex_ref (payload));

      race = dex_future_first (dex_channel_receive (channel), dex_ref (closed), NULL);
      ASSERT_STATUS (race, DEX_FUTURE_STATUS_PENDING);

      /* The idle emits a signal only after this fiber suspends in await. */
      handler = g_signal_connect (cancellable, "cancelled", G_CALLBACK (channel_cancelled_cb), closed);
      g_source_set_callback (source, channel_cancel_idle_cb, cancellable, NULL);
      g_source_attach (source, g_main_context_get_thread_default ());

      message = dex_await_boxed (dex_ref (race), &error);
      g_assert_null (message);
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
      ASSERT_STATUS (race, DEX_FUTURE_STATUS_REJECTED);
      ASSERT_STATUS (payload, DEX_FUTURE_STATUS_PENDING);

      /* Cancellation must wake the fiber without help from channel closure. */
      g_assert_true (dex_channel_can_receive (channel));
      g_clear_signal_handler (&handler, cancellable);
      g_source_destroy (source);
      dex_channel_close_receive (channel);
      dex_promise_resolve_boxed (payload, G_TYPE_BYTES, g_bytes_new_static ("message", 7));
      ASSERT_STATUS (race, DEX_FUTURE_STATUS_REJECTED);
    }
}

int
main (int argc,
      char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Dex/TestSuite/Channel/basic", test_channel_basic);
  g_test_add_func ("/Dex/TestSuite/Channel/recv_first", test_channel_recv_first);
  g_test_add_func ("/Dex/TestSuite/Channel/receive_with_cancellation", test_channel_receive_with_cancellation);
  dex_test_add_func ("/Dex/TestSuite/Channel/await_cancellation_from_signal", test_channel_await_cancellation_from_signal);
  g_test_add_func ("/Dex/TestSuite/Channel/receive_all_with_blocked_sender",
                   test_channel_receive_all_with_blocked_sender);
  return g_test_run ();
}
