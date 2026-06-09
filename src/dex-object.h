/*
 * dex-object.h
 *
 * Copyright 2022 Christian Hergert <christian@sourceandstack.com>
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

#include <glib-object.h>

#include "dex-version-macros.h"

G_BEGIN_DECLS

#define DEX_TYPE_OBJECT           (dex_object_get_type())
#define DEX_OBJECT(obj)           (G_TYPE_CHECK_INSTANCE_CAST(obj, DEX_TYPE_OBJECT, DexObject))
#define DEX_IS_OBJECT(obj)        (G_TYPE_CHECK_INSTANCE_TYPE(obj, DEX_TYPE_OBJECT))
#define DEX_OBJECT_TYPE(obj)      (G_TYPE_FROM_INSTANCE(obj))
#define DEX_OBJECT_TYPE_NAME(obj) (g_type_name(DEX_OBJECT_TYPE(obj)))

typedef struct _DexObject DexObject;

DEX_AVAILABLE_IN_ALL
GType       dex_object_get_type   (void) G_GNUC_CONST;
DEX_AVAILABLE_IN_ALL
GParamSpec *dex_param_spec_object (const char  *name,
                                   const char  *nick,
                                   const char  *blurb,
                                   GType        object_type,
                                   GParamFlags  flags);
DEX_AVAILABLE_IN_ALL
gpointer    dex_ref               (gpointer     object);
DEX_AVAILABLE_IN_ALL
void        dex_unref             (gpointer     object);

#ifndef __GI_SCANNER__
static inline void
dex_clear (gpointer data)
{
  DexObject **objptr = (DexObject **)data;
  DexObject *obj = *objptr;
  *objptr = NULL;
  if (obj != NULL)
    dex_unref (obj);
}

static inline gboolean
dex_set_object (DexObject **object_ptr,
                DexObject  *new_object)
{
  DexObject *old_object = *object_ptr;

  if (old_object == new_object)
    return FALSE;

  if (new_object != NULL)
    dex_ref (new_object);

  *object_ptr = new_object;

  if (old_object != NULL)
    dex_unref (old_object);

  return TRUE;
}

#if defined(__GNUC__)
# define dex_set_object(object_ptr, new_object) \
  (G_GNUC_EXTENSION ({ \
    G_STATIC_ASSERT (sizeof *(object_ptr) == sizeof (new_object)); \
    union { char *in; DexObject **out; } _object_ptr; \
    _object_ptr.in = (char *) (object_ptr); \
    (void) (0 ? *(object_ptr) = (new_object), FALSE : FALSE); \
    (dex_set_object) (_object_ptr.out, (DexObject *) new_object); \
  }))
#else
# define dex_set_object(object_ptr, new_object) \
  (0 ? *(object_ptr) = (new_object), FALSE : \
   (dex_set_object) ((DexObject **) (object_ptr), (DexObject *) (new_object)))
#endif
#endif

DEX_AVAILABLE_IN_ALL
DexObject *dex_value_dup_object  (const GValue *value);
DEX_AVAILABLE_IN_ALL
DexObject *dex_value_get_object  (const GValue *value);
DEX_AVAILABLE_IN_ALL
void       dex_value_set_object  (GValue       *value,
                                  DexObject    *object);
DEX_AVAILABLE_IN_ALL
void       dex_value_take_object (GValue       *value,
                                  DexObject    *object);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (DexObject, dex_unref)

G_END_DECLS
