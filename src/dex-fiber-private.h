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

#ifndef PACKAGE_VERSION
# error "config.h must be included before dex-fiber-private.h"
#endif

#include <glib.h>

#ifdef G_OS_UNIX
# include "dex-ucontext-private.h"
#endif

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

#ifdef G_OS_UNIX
  /* Context for the fiber. Use inline access if possible without breaking
   * alignment expectations of what can be allocated by the type system.
   */
# if ALIGN_OF_UCONTEXT > GLIB_SIZEOF_VOID_P
  ucontext_t *context;
# else
  ucontext_t context;
# endif
#endif

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

#ifdef G_OS_UNIX
# if ALIGN_OF_UCONTEXT > GLIB_SIZEOF_VOID_P
  ucontext_t *context;
# else
  ucontext_t context;
# endif
#endif

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

static inline ucontext_t *
DEX_FIBER_CONTEXT (DexFiber *fiber)
{
#if ALIGN_OF_UCONTEXT > GLIB_SIZEOF_VOID_P
  return fiber->context;
#else
  return &fiber->context;
#endif
}

G_END_DECLS
