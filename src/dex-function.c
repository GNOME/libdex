/*
 * dex-function.c
 *
 * Copyright 2022 Christian Hergert <chergert@gnome.org>
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

#include "config.h"

#include "dex-callable-private.h"
#include "dex-function.h"

#define DEX_FUNCTION_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST(klass, DEX_TYPE_FUNCTION, DexFunctionClass))
#define DEX_FUNCTION_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS(obj, DEX_TYPE_FUNCTION, DexFunctionClass))

typedef struct _DexFunction
{
  DexCallable parent_class;
} DexFunction;

typedef struct _DexFunctionClass
{
  DexCallableClass parent_class;
} DexFunctionClass;

DEX_DEFINE_ABSTRACT_TYPE (DexFunction, dex_function, DEX_TYPE_CALLABLE)

static void
dex_function_class_init (DexFunctionClass *function_class)
{
}

static void
dex_function_init (DexFunction *function)
{
}
