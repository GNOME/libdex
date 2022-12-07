/* dex-platform.c
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

#include "config.h"

#include "dex-platform.h"

#ifdef G_OS_WIN32
# include <windows.h>
#else
# include <unistd.h>
#endif

gsize
dex_get_page_size (void)
{
  static gsize page_size;

  if G_UNLIKELY (page_size == 0)
    {
#ifdef G_OS_WIN32
      SYSTEM_INFO si;
      GetSystemInfo (&si);
      page_size = si.dwPageSize;
#else
      page_size = sysconf (_SC_PAGESIZE);
#endif
    }

  return page_size;
}

gsize
dex_get_min_stack_size (void)
{
  static gsize min_stack_size;
  if G_UNLIKELY (min_stack_size == 0)
    {
#ifdef G_OS_WIN32
      /* Probably need to base this on granularity or something,
       * because the default stack size of 1MB is likely too much.
       */
      min_stack_size = 4096*16;
#else
      min_stack_size = sysconf (_SC_THREAD_STACK_MIN);
#endif
    }

  return min_stack_size;
}
