/* dex-graph.h
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#if !defined (DEX_INSIDE) && !defined (DEX_COMPILATION)
# error "Only <libdex.h> can be included directly."
#endif

#include "dex-future.h"
#include "dex-scheduler.h"

G_BEGIN_DECLS

typedef struct _DexGraphBuilder DexGraphBuilder;
typedef struct _DexGraphNode    DexGraphNode;

#define DEX_TYPE_GRAPH_BUILDER (dex_graph_builder_get_type())

DEX_AVAILABLE_IN_1_3
DexGraphNode    *dex_graph_builder_add            (DexGraphBuilder  *builder,
                                                   DexFuture        *future);
DEX_AVAILABLE_IN_1_3
gboolean         dex_graph_builder_add_dependency (DexGraphBuilder  *builder,
                                                   DexGraphNode     *node,
                                                   DexGraphNode     *dependency);
DEX_AVAILABLE_IN_1_3
DexFuture       *dex_graph_builder_build          (DexGraphBuilder  *builder,
                                                   GError          **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
void             dex_graph_builder_free           (DexGraphBuilder  *builder);
DEX_AVAILABLE_IN_1_3
GType            dex_graph_builder_get_type       (void);
DEX_AVAILABLE_IN_1_3
DexGraphBuilder *dex_graph_builder_new            (DexScheduler     *scheduler);
DEX_AVAILABLE_IN_1_3
DexGraphBuilder *dex_graph_builder_ref            (DexGraphBuilder  *builder);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (DexGraphBuilder, dex_graph_builder_free)

G_END_DECLS
