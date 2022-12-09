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
#include <unistd.h>

#include "dex-fiber.h"
#include "dex-future-private.h"
#include "dex-scheduler.h"
#include "dex-stack-private.h"

G_BEGIN_DECLS

typedef struct _DexFiberScheduler DexFiberScheduler;

typedef enum _DexFiberState
{
  DEX_FIBER_STATUS_READY,
  DEX_FIBER_STATUS_WAITING,
  DEX_FIBER_STATUS_EXITED,
} DexFiberState;

struct _DexFiber
{
  DexFuture parent_instance;

  /* Augmented link placed in either runnable or waiting queue
   * of the DexFiberScheduler.
   */
  GList link;

  /* The assigned stack */
  DexStack *stack;
  gsize stack_size;

  /* The scheduler affinity */
  DexFiberScheduler *fiber_scheduler;

  /* Origin function/data for the fiber */
  DexFiberFunc func;
  gpointer func_data;
  GDestroyNotify func_data_destroy;

  /* Context for the fiber */
  ucontext_t context;

  /* If the fiber is runnable */
  DexFiberState status : 2;
};

struct _DexFiberScheduler
{
  GSource source;

  /* Mutex held while running */
  GRecMutex rec_mutex;

  /* Pooling of unused thread stacks */
  DexStackPool *stack_pool;

  /* The running fiber */
  DexFiber *current;

  /* Queue of fibers ready to run */
  GQueue ready;

  /* Queue of fibers scheduled to run */
  GQueue waiting;

  /* Saved context when entering first fiber */
  ucontext_t context;

  /* If the scheduler is currently running */
  guint running : 1;
};

DexFiberScheduler *dex_fiber_scheduler_new (void);
DexFiber          *dex_fiber_new           (DexFiberFunc       func,
                                            gpointer           func_data,
                                            GDestroyNotify     func_data_destroy,
                                            gsize              stack_size);
void               dex_fiber_migrate_to    (DexFiber          *fiber,
                                            DexFiberScheduler *fiber_scheduler);

G_END_DECLS
