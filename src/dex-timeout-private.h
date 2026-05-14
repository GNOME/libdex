/*
 * dex-timeout-private.h
 *
 * Copyright 2026 Christian Hergert
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

#pragma once

#include "dex-timeout.h"

G_BEGIN_DECLS

DexFuture *_dex_timeout_new_for_future          (DexFuture *future,
                                                 gint64     usec) G_GNUC_WARN_UNUSED_RESULT;
DexFuture *_dex_timeout_new_for_future_msec     (DexFuture *future,
                                                 int        msec) G_GNUC_WARN_UNUSED_RESULT;
DexFuture *_dex_timeout_new_for_future_seconds  (DexFuture *future,
                                                 int        seconds) G_GNUC_WARN_UNUSED_RESULT;
DexFuture *_dex_timeout_new_for_future_deadline (DexFuture *future,
                                                 gint64     deadline) G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS
