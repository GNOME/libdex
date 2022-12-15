/* dex-fiber-context-private.h
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

#include <glib.h>

#include "dex-compat-private.h"
#include "dex-stack-private.h"

#ifdef G_OS_UNIX
# include "dex-ucontext-private.h"
#endif

#ifdef G_OS_WIN32
# include <windows.h>
#endif

G_BEGIN_DECLS

#ifdef G_OS_UNIX
/* If the system we're on has an alignment requirement of > sizeof(void*)
 * then we will allocate aligned memory for the ucontext_t instead of
 * including it inline. Otherwise, g_type_create_instance() (which only will
 * guarantee, at least via public API descriptions) a structure that is
 * guaranteed to be aligned to sizeof(void*).
 *
 * This is an issue, at minimum, on FreeBSD where x86_64 has a 16-bytes
 * alignment requirement for ucontext_t.
 */
# if ALIGN_OF_UCONTEXT > GLIB_SIZEOF_VOID_P
typedef ucontext_t *DexFiberContext;
#else
typedef ucontext_t DexFiberContext;
#endif

static inline void
dex_fiber_context_switch (DexFiberContext *old_context,
                          DexFiberContext *new_context);

#if GLIB_SIZEOF_VOID_P == 4
static inline void
dex_fiber_context_start (guint start_func,
                         guint start_data)
{
  GHookFunc func = (GHookFunc)GUINT_TO_POINTER (start_func);
  gpointer data = GUINT_TO_POINTER (start_data);

  func (data);
}
#elif GLIB_SIZEOF_VOID_P == 8
static inline void
dex_fiber_context_start (guint start_func_lo,
                         guint start_func_hi,
                         guint start_data_lo,
                         guint start_data_hi)
{
  gintptr ifunc;
  gintptr idata;
  GHookFunc func;
  gpointer data;

  ifunc = start_func_hi;
  ifunc <<= 32;
  ifunc |= start_func_lo;

  idata = start_data_hi;
  idata <<= 32;
  idata |= start_data_lo;

  func = (GHookFunc)(gpointer)ifunc;
  data = (gpointer)idata;

  func (data);
}
#endif

static inline void
_dex_fiber_context_makecontext (DexFiberContext *context,
                                DexStack        *stack,
                                GCallback        start_func,
                                gpointer         start_data)
{
  ucontext_t *ucontext;
  guint start_func_lo;
  guint start_data_lo;
# if GLIB_SIZEOF_VOID_P == 8
  guint start_func_hi;
  guint start_data_hi;
# endif

#if ALIGN_OF_UCONTEXT > GLIB_SIZEOF_VOID_P
  ucontext = *context;
#else
  ucontext = context;
#endif

  getcontext (ucontext);

  ucontext->uc_stack.ss_size = stack->size;
  ucontext->uc_stack.ss_sp = stack->ptr;
  ucontext->uc_link = 0;

  start_func_lo = GPOINTER_TO_SIZE (start_func) & 0xFFFFFFFFF;
  start_data_lo = GPOINTER_TO_SIZE (start_data) & 0xFFFFFFFFF;

#if GLIB_SIZEOF_VOID_P == 8
  start_func_hi = (GPOINTER_TO_SIZE (start_func) >> 32) & 0xFFFFFFFFF;
  start_data_hi = (GPOINTER_TO_SIZE (start_data) >> 32) & 0xFFFFFFFFF;
#endif

  makecontext (ucontext,
               G_CALLBACK (dex_fiber_context_start),
#if GLIB_SIZEOF_VOID_P == 4
               2, start_func_lo, start_data_lo
#elif GLIB_SIZEOF_VOID_P == 8
               4, start_func_lo, start_func_hi, start_data_lo, start_data_hi
#endif
              );
}

static inline void
dex_fiber_context_init (DexFiberContext *context,
                        DexStack        *stack,
                        GCallback        start_func,
                        gpointer         start_data)
{
#if ALIGN_OF_UCONTEXT > GLIB_SIZEOF_VOID_P
  *context = g_aligned_alloc (1, sizeof (ucontext_t), ALIGN_OF_UCONTEXT);
#endif

  /* If stack is NULL, then this is a context used to save state
   * such as from the original stack in the fiber scheduler.
   */
  if (stack != NULL)
    _dex_fiber_context_makecontext (context, stack, start_func, start_data);
}

static inline void
dex_fiber_context_clear (DexFiberContext *context)
{
#if ALIGN_OF_UCONTEXT > GLIB_SIZEOF_VOID_P
  g_aligned_free (*context);
#endif
}

static inline void
dex_fiber_context_switch (DexFiberContext *old_context,
                          DexFiberContext *new_context)
{
#if ALIGN_OF_UCONTEXT > GLIB_SIZEOF_VOID_P
  swapcontext (*old_context, *new_context);
#else
  swapcontext (old_context, new_context);
#endif
}
#endif

G_END_DECLS
