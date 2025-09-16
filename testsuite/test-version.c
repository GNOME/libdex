/* test-version.c
 *
 * Copyright 2025 Christian Hergert <chergert@redhat.com>
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

#include <libdex.h>

static void
test_version (void)
{
  G_GNUC_UNUSED const char *str = DEX_VERSION_S;
  G_GNUC_UNUSED int major = DEX_MAJOR_VERSION;
  G_GNUC_UNUSED int minor = DEX_MINOR_VERSION;
  G_GNUC_UNUSED int micro = DEX_MICRO_VERSION;
}

int
main (int argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Dex/Version/basic", test_version);
  return g_test_run ();
}
