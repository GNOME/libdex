/*
 * dex-work-queue-private.h
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

#include "dex-scheduler-private.h"

G_BEGIN_DECLS

typedef struct _DexWorkQueue DexWorkQueue;

DexWorkQueue *dex_work_queue_new           (void);
DexWorkQueue *dex_work_queue_ref           (DexWorkQueue *work_queue);
void          dex_work_queue_unref         (DexWorkQueue *work_queue);
void          dex_work_queue_push          (DexWorkQueue *work_queue,
                                            DexWorkItem   work_item);
gboolean      dex_work_queue_pop           (DexWorkQueue *work_queue,
                                            DexWorkItem  *out_work_item);
GSource      *dex_work_queue_create_source (DexWorkQueue *work_queue);

G_END_DECLS
