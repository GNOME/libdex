/* dex-fd.c
 *
 * Copyright 2024 Christian Hergert <chergert@redhat.com>
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

#include <unistd.h>

#include "dex-fd-private.h"

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

void
dex_fd_free (DexFD *fd)
{
  int real = dex_fd_steal (fd);
  if (real != -1)
    close (real);
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
