/*
 * dex-semaphore.c
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

#include <errno.h>
#include <unistd.h>

#ifdef HAVE_EVENTFD
# include <sys/eventfd.h>
#endif

#ifdef HAVE_LIBURING
# include <liburing.h>
#endif

#include <gio/gio.h>

#include "dex-aio-private.h"
#include "dex-object-private.h"
#include "dex-promise.h"
#include "dex-semaphore-private.h"

struct _DexSemaphore
{
  DexObject parent_instance;
  int eventfd;
};

typedef struct _DexSemaphoreClass
{
  DexObjectClass parent_class;
} DexSemaphoreClass;

DEX_DEFINE_FINAL_TYPE (DexSemaphore, dex_semaphore, DEX_TYPE_OBJECT)

DexSemaphore *
dex_semaphore_new (void)
{
  return (DexSemaphore *)g_type_create_instance (DEX_TYPE_SEMAPHORE);
}

static void
dex_semaphore_finalize (DexObject *object)
{
  DexSemaphore *semaphore = (DexSemaphore *)object;

  if (semaphore->eventfd != -1)
    {
      close (semaphore->eventfd);
      semaphore->eventfd = -1;
    }

  DEX_OBJECT_CLASS (dex_semaphore_parent_class)->finalize (object);
}

static void
dex_semaphore_class_init (DexSemaphoreClass *semaphore_class)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (semaphore_class);

  object_class->finalize = dex_semaphore_finalize;
}

static void
dex_semaphore_init (DexSemaphore *semaphore)
{
#ifdef HAVE_EVENTFD
  semaphore->eventfd = eventfd (0, EFD_SEMAPHORE);
#else
# error "DexSemaphore is not yet supported on your platform"
#endif
}

void
dex_semaphore_post (DexSemaphore *semaphore)
{
  return dex_semaphore_post_many (semaphore, 1);
}

void
dex_semaphore_post_many (DexSemaphore *semaphore,
                         guint         count)
{
  guint64 counter = count;

  /* Writes to eventfd are 64-bit integers and always atomic. Anything
   * other than sizeof(counter) indicates failure and we are not prepared
   * to handle that as it shouldn't happen. Just bail.
   */
  if (write (semaphore->eventfd, &counter, sizeof counter) != sizeof counter)
    {
      int errsv = errno;
      g_error ("Failed to post semaphore counter: %s",
               g_strerror (errsv));
    }
}

DexFuture *
dex_semaphore_wait (DexSemaphore *semaphore)
{
  static gint64 trash_value;

  g_return_val_if_fail (DEX_IS_SEMAPHORE (semaphore), NULL);

  if (semaphore->eventfd < 0)
    return DEX_FUTURE (dex_future_new_reject (G_IO_ERROR,
                                              G_IO_ERROR_CLOSED,
                                              "The semaphore has already been closed"));

  return dex_aio_read (NULL,
                       semaphore->eventfd,
                       &trash_value,
                       sizeof trash_value,
                       -1);
}
