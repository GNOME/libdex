/*
 * dex-coroutine-private.h
 *
 * Copyright 2024 Christian Hergert <christian@sourceandstack.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "dex-coroutine.h"
#include "dex-scheduler.h"

G_BEGIN_DECLS

typedef struct _DexCoroutine          DexCoroutine;
typedef struct _DexCoroutineScheduler DexCoroutineScheduler;

DexCoroutine          *dex_coroutine_new                (DexCoroutineFunc       func,
                                                         gpointer               user_data,
                                                         GDestroyNotify         user_data_destroy);
DexCoroutineScheduler *dex_coroutine_scheduler_new      (void);
void                   dex_coroutine_scheduler_register (DexCoroutineScheduler *scheduler,
                                                         DexCoroutine          *coroutine);

G_END_DECLS
