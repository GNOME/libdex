/* libdex.h
 *
 * Copyright 2022 Christian Hergert
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define DEX_INSIDE
# include "dex-callable.h"
# include "dex-cancellable.h"
# include "dex-enums.h"
# include "dex-function.h"
# include "dex-future.h"
# include "dex-init.h"
# include "dex-main-scheduler.h"
# include "dex-object.h"
# include "dex-promise.h"
# include "dex-scheduler.h"
# include "dex-tasklet.h"
# include "dex-timeout.h"
# include "dex-version.h"
# include "dex-version-macros.h"
#undef DEX_INSIDE

G_END_DECLS
