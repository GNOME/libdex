/* dex-compat-private.h
 *
 * Copyright 2022 Christian Hergert <christian@hergert.me>
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

#pragma once

#include <glib.h>

G_BEGIN_DECLS

static inline void
_g_source_set_static_name (GSource    *source,
                           const char *name)
{
#if GLIB_CHECK_VERSION(2, 70, 0)
  g_source_set_static_name (source, name);
#else
  g_source_set_name (source, name);
#endif
}

G_END_DECLS
