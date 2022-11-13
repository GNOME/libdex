/* dex-block.c
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

#include "config.h"

#include "dex-block-private.h"

typedef struct _DexBlock
{
  DexFuture          parent_instance;
  DexScheduler      *scheduler;
  DexFuture         *awaiting;
  DexFutureCallback  callback;
  gpointer           callback_data;
  GDestroyNotify     callback_data_destroy;
  DexBlockKind       kind : 2;
  guint              handled : 1;
} DexBlock;

typedef struct _DexBlockClass
{
  DexFutureClass parent_class;
} DexBlockClass;

DEX_DEFINE_FINAL_TYPE (DexBlock, dex_block, DEX_TYPE_FUTURE)

static gboolean
dex_block_handles (DexBlock  *block,
                   DexFuture *future)
{
  g_assert (DEX_IS_BLOCK (block));
  g_assert (DEX_IS_FUTURE (future));

  switch (dex_future_get_status (future))
    {
    case DEX_FUTURE_STATUS_RESOLVED:
      return (block->kind & DEX_BLOCK_KIND_THEN) != 0;

    case DEX_FUTURE_STATUS_REJECTED:
      return (block->kind & DEX_BLOCK_KIND_CATCH) != 0;

    case DEX_FUTURE_STATUS_PENDING:
    default:
      return FALSE;
    }
}

static gboolean
dex_block_propagate (DexFuture *future,
                     DexFuture *completed)
{
  DexBlock *block = DEX_BLOCK (future);
  DexFuture *awaiting;
  gboolean do_callback = FALSE;

  g_assert (DEX_IS_BLOCK (block));
  g_assert (DEX_IS_FUTURE (completed));
  g_assert (dex_future_get_status (completed) != DEX_FUTURE_STATUS_PENDING);

  /* Mark result as handled as we don't want to execute the callback
   * again when a possible secondary DexFuture propagates completion
   * to us. That would only happen if @callback returns a DexFuture
   * which completes.
   */
  dex_object_lock (future);
  if (!block->handled)
    do_callback = block->handled = TRUE;
  awaiting = g_steal_pointer (&block->awaiting);
  dex_object_unlock (future);

  /* Release the future we were waiting on */
  dex_clear (&awaiting);

  /* Run the callback, possibly getting a future back to delay further
   * processing until it's completed.
   */
  if (do_callback && dex_block_handles (block, completed))
    {
      DexFuture *delayed = block->callback (completed, block->callback_data);

      /* If we got a future then we need to chain to it so that we get
       * a second propagation callback with the resolved or rejected
       * value for resolution.
       */
      if (delayed != NULL)
        {
          dex_object_lock (future);
          block->awaiting = delayed;
          dex_object_unlock (future);

          dex_future_chain (delayed, future);

          return TRUE;
        }
    }

  return FALSE;
}

static void
dex_block_finalize (DexObject *object)
{
  DexBlock *block = DEX_BLOCK (object);

  if (block->callback_data_destroy)
    {
      block->callback_data_destroy (block->callback_data);
      block->callback_data_destroy = NULL;
      block->callback_data = NULL;
      block->callback = NULL;
    }

  dex_clear (&block->awaiting);
  dex_clear (&block->scheduler);

  DEX_OBJECT_CLASS (dex_block_parent_class)->finalize (object);
}

static void
dex_block_class_init (DexBlockClass *block_class)
{
  DexObjectClass *object_class = DEX_OBJECT_CLASS (block_class);
  DexFutureClass *future_class = DEX_FUTURE_CLASS (block_class);

  object_class->finalize = dex_block_finalize;

  future_class->propagate = dex_block_propagate;
}

static void
dex_block_init (DexBlock *block)
{
  block->scheduler = dex_scheduler_ref_thread_default ();
}

/**
 * dex_block_new:
 * @future: (transfer full): a #DexFuture to process
 * @kind: the kind of block
 * @callback: (scope async): the callback for the block
 * @callback_data: the data for the callback
 * @callback_data_destroy: closure destroy for @callback_data
 *
 * Creates a new block that will process the result of @future.
 *
 * The result of @callback will be assigned to the future returned
 * from this method.
 *
 * Returns: (transfer full): a #DexBlock
 */
DexFuture *
dex_block_new (DexFuture         *future,
               DexBlockKind       kind,
               DexFutureCallback  callback,
               gpointer           callback_data,
               GDestroyNotify     callback_data_destroy)
{
  DexBlock *block;

  g_return_val_if_fail (DEX_IS_FUTURE (future), NULL);

  block = (DexBlock *)g_type_create_instance (dex_block_type);
  block->awaiting = future;
  block->kind = kind;
  block->callback = callback;
  block->callback_data = callback_data;
  block->callback_data_destroy = callback_data_destroy;

  dex_future_chain (future, DEX_FUTURE (block));

  return DEX_FUTURE (block);
}
