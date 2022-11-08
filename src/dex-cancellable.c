/*
 * dex-cancellable.c
 *
 * Copyright 2022 Christian Hergert <chergert@gnome.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <gio/gio.h>

#include "dex-cancellable.h"
#include "dex-private.h"

typedef struct _DexCancellable
{
  DexFuture parent_instance;
} DexCancellable;

typedef struct _DexCancellableClass
{
  DexFutureClass parent_class;
} DexCancellableClass;

DEX_DEFINE_FINAL_TYPE (DexCancellable, dex_cancellable, DEX_TYPE_FUTURE)

static void
dex_cancellable_class_init (DexCancellableClass *cancellable_class)
{
}

static void
dex_cancellable_init (DexCancellable *cancellable)
{
}

DexCancellable *
dex_cancellable_new (void)
{
  return (DexCancellable *)g_type_create_instance (DEX_TYPE_CANCELLABLE);
}

void
dex_cancellable_cancel (DexCancellable *cancellable)
{
  g_return_if_fail (DEX_IS_CANCELLABLE (cancellable));

  dex_future_complete (DEX_FUTURE (cancellable),
                       NULL,
                       g_error_new_literal (G_IO_ERROR,
                                            G_IO_ERROR_CANCELLED,
                                            "Operation cancelled"));
}
