/* dex-version.c
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

#include "dex-version-macros.h"

/**
 * dex_get_major_version:
 *
 * Gets the major version number equivalent to `DEX_MAJOR_VERSION`
 * at compile time of libdex.
 *
 * Since: 1.1
 */
int
dex_get_major_version (void)
{
  return DEX_MAJOR_VERSION;
}

/**
 * dex_get_minor_version:
 *
 * Gets the minor version number equivalent to `DEX_MINOR_VERSION`
 * at compile time of libdex.
 *
 * Since: 1.1
 */
int
dex_get_minor_version (void)
{
  return DEX_MINOR_VERSION;
}

/**
 * dex_get_micro_version:
 *
 * Gets the micro version number equivalent to `DEX_MICRO_VERSION`
 * at compile time of libdex.
 *
 * Since: 1.1
 */
int
dex_get_micro_version (void)
{
  return DEX_MICRO_VERSION;
}
