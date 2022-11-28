/*
 * dex-timeout.c
 *
 * Copyright 2022 Christian Hergert <chergert@gnome.org>
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

#include <gio/gio.h>

#include "dex-error.h"
#include "dex-future-private.h"
#include "dex-timeout.h"

typedef struct _DexTimeout
{
  DexFuture parent_instance;
  GSource *source;
} DexTimeout;

typedef struct _DexTimeoutClass
{
  DexFutureClass parent_class;
} DexTimeoutClass;

DEX_DEFINE_FINAL_TYPE (DexTimeout, dex_timeout, DEX_TYPE_FUTURE)

static void
dex_timeout_finalize (DexObject *object)
{
  DexTimeout *timeout = DEX_TIMEOUT (object);

  if (timeout->source != NULL)
    {
      g_source_destroy (timeout->source);
      g_clear_pointer (&timeout->source, g_source_unref);
    }

  DEX_OBJECT_CLASS (dex_timeout_parent_class)->finalize (object);
}

static void
dex_timeout_class_init (DexTimeoutClass *timeout_class)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (timeout_class);

  object_class->finalize = dex_timeout_finalize;
}

static void
dex_timeout_init (DexTimeout *timeout)
{
}

static void
clear_weak_ref (gpointer data)
{
  dex_weak_ref_clear (data);
  g_free (data);
}

static gboolean
dex_timeout_source_func (gpointer data)
{
  DexWeakRef *wr = data;
  DexTimeout *timeout = dex_weak_ref_get (wr);

  g_assert (!timeout || DEX_IS_TIMEOUT (timeout));

  if (timeout != NULL)
    {
      dex_future_complete (DEX_FUTURE (timeout),
                           NULL,
                           g_error_new_literal (DEX_ERROR,
                                                DEX_ERROR_TIMED_OUT,
                                                "Operation timed out"));

      dex_object_lock (timeout);
      g_clear_pointer (&timeout->source, g_source_unref);
      dex_object_unlock (timeout);

      dex_unref (timeout);
    }

  return G_SOURCE_REMOVE;
}

DexFuture *
dex_timeout_new_deadline (gint64 deadline)
{
  static const char *name;
  DexTimeout *timeout;
  DexWeakRef *wr;

  if G_UNLIKELY (name == NULL)
    name = g_intern_static_string ("[dex-timeout]");

  timeout = (DexTimeout *)g_type_create_instance (DEX_TYPE_TIMEOUT);

  wr = g_new0 (DexWeakRef, 1);
  dex_weak_ref_init (wr, timeout);

  timeout->source = g_timeout_source_new (0);
  g_source_set_ready_time (timeout->source, deadline);
  g_source_set_static_name (timeout->source, name);
  g_source_set_priority (timeout->source, G_PRIORITY_DEFAULT);
  g_source_set_callback (timeout->source,
                         dex_timeout_source_func,
                         wr, clear_weak_ref);

  /* TODO: allow attaching to the current scheduler
   *
   * Currently, this will end up on the main context (so the main scheduler for
   * the application) which could cause a ping-pong between the main scheduler and
   * other thread pool schedulers for work items.
   *
   * Of course, this is only really an issue for situations where you are doing
   * user timeouts, but if it where to be used for something more timing sensitive
   * like deadlines, you'd end up in a lot of cross-thread operations.
   *
   * Instead, if we require that all schedulers support a GMainContext for main
   * loop integration, we could connect to the current schedulers main loop (which
   * should attach to the current threads main loop if the scheduler is a thread
   * pool situation).
   *
   * But to do this, we need to be able to have more control than GThreadPool will
   * give us, and therefore need to do our own threadpool. We probably want to do
   * that anyway though because GThreadPool does not scale with thread count very
   * well to begin with.
   */
  g_source_attach (timeout->source, NULL);

  return DEX_FUTURE (timeout);
}

DexFuture *
dex_timeout_new_seconds (int seconds)
{
  gint64 usec = G_USEC_PER_SEC * seconds;
  return dex_timeout_new_deadline (g_get_monotonic_time () + usec);
}

DexFuture *
dex_timeout_new_msec (int msec)
{
  gint64 usec = (G_USEC_PER_SEC/1000L) * msec;
  return dex_timeout_new_deadline (g_get_monotonic_time () + usec);
}

DexFuture *
dex_timeout_new_usec (gint64 usec)
{
  return dex_timeout_new_deadline (g_get_monotonic_time () + usec);
}

void
dex_timeout_postpone_until (DexTimeout *timeout,
                            gint64      deadline)
{
  g_return_if_fail (DEX_IS_TIMEOUT (timeout));

  dex_object_lock (timeout);
  if (timeout->source != NULL)
    g_source_set_ready_time (timeout->source, deadline);
  dex_object_unlock (timeout);
}
