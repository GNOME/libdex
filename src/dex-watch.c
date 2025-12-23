/* dex-watch.c
 *
 * Copyright 2025 Christian Hergert
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

/*
 * DexWatch:
 *
 * This is just simple integration for waiting for a single-shot poll()
 * event to return. Sort of like g_io_add_watch().
 */

#include "dex-compat-private.h"
#include "dex-future-private.h"
#include "dex-scheduler.h"
#include "dex-watch-private.h"

typedef struct _DexWatch
{
  DexFuture parent_instance;
  GSource *source;
  gpointer tag;
} DexWatch;

typedef struct _DexWatchClass
{
  DexFutureClass parent_class;
} DexWatchClass;

DEX_DEFINE_FINAL_TYPE (DexWatch, dex_watch, DEX_TYPE_FUTURE)

static void
dex_watch_discard (DexFuture *future)
{
  DexWatch *watch = DEX_WATCH (future);

  if (watch->source != NULL)
    g_source_destroy (watch->source);
}

static void
dex_watch_finalize (DexObject *object)
{
  DexWatch *watch = DEX_WATCH (object);

  if (watch->source != NULL)
    {
      if (!g_source_is_destroyed (watch->source))
        {
          g_critical ("`%s` destroyed while source was active. "
                      "This is likely a bug as no future is holding a reference to %p",
                      DEX_OBJECT_TYPE_NAME (object), object);
          g_source_destroy (watch->source);
        }

      g_clear_pointer (&watch->source, g_source_unref);
    }

  DEX_OBJECT_CLASS (dex_watch_parent_class)->finalize (object);
}

static void
dex_watch_class_init (DexWatchClass *watch_class)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (watch_class);
  DexFutureClass *future_class = DEX_FUTURE_CLASS (watch_class);

  object_class->finalize = dex_watch_finalize;

  future_class->discard = dex_watch_discard;
}

static void
dex_watch_init (DexWatch *watch)
{
}

static void
clear_weak_ref (gpointer data)
{
  dex_weak_ref_clear (data);
  g_free (data);
}

static gboolean
dex_watch_source_func (gpointer data)
{
  DexWeakRef *wr = data;
  DexWatch *watch = dex_weak_ref_get (wr);

  g_assert (!watch || DEX_IS_WATCH (watch));

  if (watch != NULL)
    {
      GValue value = G_VALUE_INIT;
      int revents;

      dex_object_lock (watch);

      if (watch->source && watch->tag)
        revents = g_source_query_unix_fd (watch->source, watch->tag);
      else
        revents = 0;

      dex_object_unlock (watch);

      g_value_init (&value, G_TYPE_INT);
      g_value_set_int (&value, revents);
      dex_future_complete (DEX_FUTURE (watch), &value, NULL);
      g_value_unset (&value);

      dex_object_lock (watch);
      g_clear_pointer (&watch->source, g_source_unref);
      dex_object_unlock (watch);

      dex_unref (watch);
    }

  return G_SOURCE_REMOVE;
}

static gboolean
dex_watch_source_dispatch (GSource     *source,
                           GSourceFunc  callback,
                           gpointer     user_data)
{
  return callback (user_data);
}

static GSourceFuncs source_funcs = {
  .dispatch = dex_watch_source_dispatch,
};

DexFuture *
dex_watch_new (int fd,
               int events)
{
  static const char *name;
  DexScheduler *scheduler;
  DexWatch *watch;
  DexWeakRef *wr;

  if G_UNLIKELY (name == NULL)
    name = g_intern_static_string ("[dex-watch]");

  watch = (DexWatch *)dex_object_create_instance (DEX_TYPE_WATCH);

  wr = g_new0 (DexWeakRef, 1);
  dex_weak_ref_init (wr, watch);

  watch->source = g_source_new (&source_funcs, sizeof (GSource));
  watch->tag = g_source_add_unix_fd (watch->source, fd, events);
  _g_source_set_static_name (watch->source, name);
  g_source_set_priority (watch->source, G_PRIORITY_DEFAULT);
  g_source_set_callback (watch->source,
                         dex_watch_source_func,
                         wr, clear_weak_ref);

  if (!(scheduler = dex_scheduler_get_thread_default ()))
    scheduler = dex_scheduler_get_default ();

  g_source_attach (watch->source, dex_scheduler_get_main_context (scheduler));

  return DEX_FUTURE (watch);
}
