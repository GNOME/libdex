/* dex-fd.c
 *
 * Copyright 2024 Christian Hergert <christian@sourceandstack.com>
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

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <gio/gio.h>

#include "dex-aio.h"
#include "dex-fd-private.h"
#include "dex-thread-storage-private.h"

#ifdef G_OS_WIN32
#include <io.h>
#endif

int
dex_fd_peek (const DexFD *fd)
{
  if (fd == NULL)
    return -1;

  return *(int *)fd;
}

int
dex_fd_steal (DexFD *fd)
{
  if (fd == NULL)
    return -1;

  return g_steal_fd ((int *)fd);
}

static void
dex_fd_close_worker (GTask        *task,
                     gpointer      source_object,
                     gpointer      task_data,
                     GCancellable *cancellable)
{
  int fd = GPOINTER_TO_INT (task_data);

  close (fd);
  g_task_return_boolean (task, TRUE);
}

void
_dex_fd_close_in_thread (int fd)
{
  GTask *task;

  g_assert (fd >= 0);

  task = g_task_new (NULL, NULL, NULL, NULL);
  g_task_set_task_data (task, GINT_TO_POINTER (fd), NULL);
  g_task_run_in_thread (task, dex_fd_close_worker);
  g_object_unref (task);
}

void
dex_fd_free (DexFD *fd)
{
  int real = dex_fd_steal (fd);

  if (real != -1)
    {
      DexThreadStorage *storage = dex_thread_storage_peek ();

      if (storage != NULL && storage->aio_context != NULL)
        dex_unref (dex_aio_close (storage->aio_context, real));
      else
        _dex_fd_close_in_thread (real);
    }

  g_free (fd);
}

DexFD *
dex_fd_dup (const DexFD *fd)
{
  int real = dex_fd_peek (fd);

  if (real == -1)
    return NULL;

  real = dup (real);

  return g_memdup2 (&real, sizeof real);
}

G_DEFINE_BOXED_TYPE (DexFD, dex_fd, dex_fd_dup, dex_fd_free)
