/* dex-socket-wait.c
 *
 * Copyright 2026 Christian Hergert
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

#include "config.h"

#include "dex-compat-private.h"
#include "dex-future-private.h"
#include "dex-scheduler.h"
#include "dex-socket-wait-private.h"

struct _DexSocketWait
{
  DexFuture parent_instance;
  GSource *source;
};

typedef struct _DexSocketWaitClass
{
  DexFutureClass parent_class;
} DexSocketWaitClass;

DEX_DEFINE_FINAL_TYPE (DexSocketWait, dex_socket_wait, DEX_TYPE_FUTURE)

static void
dex_socket_wait_discard (DexFuture *future)
{
  DexSocketWait *self = DEX_SOCKET_WAIT (future);

  if (self->source != NULL)
    g_source_destroy (self->source);
}

static void
dex_socket_wait_finalize (DexObject *object)
{
  DexSocketWait *self = DEX_SOCKET_WAIT (object);

  if (self->source != NULL)
    {
      if (!g_source_is_destroyed (self->source))
        {
          g_critical ("`%s` destroyed while source was active. "
                      "This is likely a bug as no future is holding a reference to %p",
                      DEX_OBJECT_TYPE_NAME (object), object);
          g_source_destroy (self->source);
        }

      g_clear_pointer (&self->source, g_source_unref);
    }

  DEX_OBJECT_CLASS (dex_socket_wait_parent_class)->finalize (object);
}

static void
dex_socket_wait_class_init (DexSocketWaitClass *klass)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (klass);
  DexFutureClass *future_class = DEX_FUTURE_CLASS (klass);

  object_class->finalize = dex_socket_wait_finalize;

  future_class->discard = dex_socket_wait_discard;
}

static void
dex_socket_wait_init (DexSocketWait *self)
{
}

static void
dex_socket_wait_clear_weak_ref (gpointer data)
{
  dex_weak_ref_clear (data);
  g_free (data);
}

static gboolean
dex_socket_wait_source_func (GSocket      *socket,
                             GIOCondition condition,
                             gpointer     data)
{
  DexWeakRef *wr = data;
  DexSocketWait *self = dex_weak_ref_get (wr);

  g_assert (!self || DEX_IS_SOCKET_WAIT (self));
  g_assert (!self || G_IS_SOCKET (socket));

  if (self != NULL)
    {
      GValue value = G_VALUE_INIT;

      g_value_init (&value, G_TYPE_IO_CONDITION);
      g_value_set_flags (&value, condition);
      dex_future_complete (DEX_FUTURE (self), &value, NULL);
      g_value_unset (&value);

      dex_object_lock (self);
      g_clear_pointer (&self->source, g_source_unref);
      dex_object_unlock (self);

      dex_unref (self);
    }

  return G_SOURCE_REMOVE;
}

DexFuture *
dex_socket_wait_new (GSocket      *socket,
                     GIOCondition  condition)
{
  static const char *name;
  DexScheduler *scheduler;
  DexSocketWait *self;
  DexWeakRef *wr;

  g_return_val_if_fail (G_IS_SOCKET (socket), NULL);
  g_return_val_if_fail (condition != 0, NULL);

  if G_UNLIKELY (name == NULL)
    name = g_intern_static_string ("[dex-socket-wait]");

  self = (DexSocketWait *)dex_object_create_instance (DEX_TYPE_SOCKET_WAIT);

  wr = g_new0 (DexWeakRef, 1);
  dex_weak_ref_init (wr, self);

  self->source = g_socket_create_source (socket, condition, NULL);
  _g_source_set_static_name (self->source, name);
  g_source_set_priority (self->source, G_PRIORITY_DEFAULT);
  g_source_set_callback (self->source,
                         (GSourceFunc)dex_socket_wait_source_func,
                         wr,
                         dex_socket_wait_clear_weak_ref);

  if (!(scheduler = dex_scheduler_get_thread_default ()))
    scheduler = dex_scheduler_get_default ();

  g_source_attach (self->source, dex_scheduler_get_main_context (scheduler));

  return DEX_FUTURE (self);
}
