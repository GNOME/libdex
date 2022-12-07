/* dex-fiber-private.h
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

#pragma once

#include <ucontext.h>

#include "dex-object-private.h"
#include "dex-fiber.h"

G_BEGIN_DECLS

typedef void (*DexFiberFunc) (gpointer data);

struct _DexFiber
{
  DexObject    parent_instance;
  ucontext_t   context;
  DexFiberFunc func;
  gpointer     func_data;
};

DexFiber *dex_fiber_new     (DexFiberFunc  func,
                             gpointer      func_data,
                             gpointer      stack,
                             gsize         stack_size);
void      dex_fiber_swap_to (DexFiber     *fiber,
                             DexFiber     *to);

G_END_DECLS
