/* test-jiffy.c
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

#include <glib.h>

typedef void (*Callback) (gpointer data);

typedef struct _Item
{
  Callback callback;
  gpointer callback_data;
} Item;

#define JIFFY_TYPE Item
#define JIFFY_BUFFER_SIZE 194
# include "jiffy.h"
#undef JIFFY_TYPE
#undef JIFFY_BUFFER_SIZE

#if GLIB_SIZEOF_VOID_P == 8
G_STATIC_ASSERT (sizeof (JiffyBuffer) == 4096);
#endif

static void
test_jiffy_basic (void)
{
  JiffyQueue queue;
  jiffy_queue_init (&queue);
  jiffy_queue_clear (&queue);
}

int
main (int argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Dex/TestSuite/Jiffy/basic", test_jiffy_basic);
  return g_test_run ();
}
