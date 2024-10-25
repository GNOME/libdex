/* dex-fd-private.h
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

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define DEX_TYPE_FD (dex_fd_get_type())

typedef struct _DexFD DexFD;

GType  dex_fd_get_type (void) G_GNUC_CONST;
int    dex_fd_peek     (const DexFD *fd);
int    dex_fd_steal    (DexFD       *fd);
DexFD *dex_fd_dup      (const DexFD *fd);
void   dex_fd_free     (DexFD       *fd);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (DexFD, dex_fd_free)

G_END_DECLS
