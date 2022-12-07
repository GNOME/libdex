/* test-fiber.c
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

#include <ucontext.h>
#include <unistd.h>

#include <libdex.h>

#include "dex-fiber-private.h"

static int test_arg = 123;
static ucontext_t context;

static void
fiber_func (gpointer data)
{
  g_assert_true (data == &test_arg);

  g_printerr ("fiber func\n");
}

static void
test_fiber_basic (void)
{
  DexFiber *fiber;
  gsize stack_size = 4096*4;
  gpointer stack = g_aligned_alloc (1, stack_size, getpagesize ());

  /* TODO: stack creation helper w/ guard page */

  fiber = dex_fiber_new (fiber_func, &test_arg, stack, stack_size);
  swapcontext (&context, &fiber->context);

  g_assert_not_reached ();
}

int
main (int argc,
      char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Dex/TestSuite/Fiber/basic", test_fiber_basic);
  return g_test_run ();
}
