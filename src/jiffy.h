/* jiffy.h
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

G_BEGIN_DECLS

#ifndef JIFFY_TYPE
# error "You must define JIFFY_TYPE before including jiffy.h"
#endif

#ifndef JIFFY_BUFFER_SIZE
/* The JIFFY_BUFFER_SIZE contains the number of JIFFY_TYPE to store within a
 * single JiffyBuffer. For example, to store 100 pointers in each
 * JiffyBuffer, you would:
 *
 *   #define JIFFY_BUFFER_SIZE 100
 *
 * Which on a system with 64-bit/8-byte pointers would give you a
 * sizeof(JiffyBuffer) close to (100*8+100*4) = 1200 bytes. Generally you
 * want to figure out the maths to end up with something that will roughly
 * keep you near a page boundary.
 */
# error "You must define JIFFY_BUFFER_SIZE before including jiffy.h"
#endif

#ifndef JIFFY_CACHELINE_SIZE
/* On most architectures this will be 64 bytes. If your system is different,
 * you might find value in tweaking this. It does have the requirement that
 * your cacheline must be a multiple of either 8-bytes or a pointer,
 * whichever is larger.
 */
# define JIFFY_CACHELINE_SIZE 64
#endif

#ifndef JIFFY_PAGE_SIZE
/* The page size for your system. Most systems today are still 4096 but if
 * you're on a CPU like an M1 with a properly configured operating system,
 * you might have something larger like (4096*4).
 */
# define JIFFY_PAGE_SIZE 4096
#endif

typedef struct _JiffyQueue  JiffyQueue;
typedef struct _JiffyBuffer JiffyBuffer;

typedef enum _JiffyState
{
  JIFFY_STATE_EMPTY   = 0,
  JIFFY_STATE_SET     = 1,
  JIFFY_STATE_HANDLED = 2,
} JiffyState;

struct _JiffyQueue
{
  /* We want @head and @tail to be on separate cachelines to prevent any
   * false sharing. Out of caution, we make each of them the only thing on
   * their cacheline.
   */
  JiffyBuffer *head;
  gpointer     _padding1[(JIFFY_CACHELINE_SIZE/GLIB_SIZEOF_VOID_P)-1];
  JiffyBuffer *tail;
  gpointer     _padding2[(JIFFY_CACHELINE_SIZE/GLIB_SIZEOF_VOID_P)-1];
  guint64      global_tail;
  guint64      _padding3[(JIFFY_CACHELINE_SIZE/sizeof(guint64))-1];
};

G_STATIC_ASSERT (JIFFY_CACHELINE_SIZE % GLIB_SIZEOF_VOID_P == 0);
G_STATIC_ASSERT (JIFFY_CACHELINE_SIZE % sizeof (guint64) == 0);

struct _JiffyBuffer
{
  _Alignas(JIFFY_CACHELINE_SIZE) JIFFY_TYPE items[JIFFY_BUFFER_SIZE];
  _Alignas(JIFFY_CACHELINE_SIZE) guint state[JIFFY_BUFFER_SIZE];
  _Alignas(JIFFY_CACHELINE_SIZE) JiffyBuffer *next;
  _Alignas(JIFFY_CACHELINE_SIZE) JiffyBuffer *prev;
  guint head;
  guint pos;
};

static inline JiffyBuffer *
jiffy_buffer_new (void)
{
  JiffyBuffer *buffer = g_aligned_alloc0 (1, sizeof (JiffyBuffer), JIFFY_PAGE_SIZE);
  buffer->pos = 1;
  return buffer;
}

static inline void
jiffy_buffer_free (JiffyBuffer *buffer)
{
  g_aligned_free (buffer);
}

static inline void
jiffy_queue_init (JiffyQueue *queue)
{
  queue->head = jiffy_buffer_new ();
  queue->tail = queue->head;
  queue->global_tail = 0;
}

static inline void
jiffy_queue_clear (JiffyQueue *queue)
{
  JiffyBuffer *buffer = g_atomic_pointer_get (&queue->head);

  while (buffer != NULL)
    {
      JiffyBuffer *next = g_atomic_pointer_get (&buffer->next);
      jiffy_buffer_free (buffer);
      buffer = next;
    }

  queue->head = NULL;
  queue->tail = NULL;
  queue->global_tail = 0;
}

static inline void
jiffy_queue_enqueue (JiffyQueue *queue,
                     JIFFY_TYPE  item)
{
}

static inline gboolean
jiffy_queue_dequeue (JiffyQueue *queue,
                     JIFFY_TYPE *item)
{
  return FALSE;
}

G_END_DECLS
