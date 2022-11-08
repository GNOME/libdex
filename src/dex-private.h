/*
 * dex-private.h
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

#pragma once

#include "dex-object.h"
#include "dex-future.h"
#include "dex-future-set.h"

G_BEGIN_DECLS

#define DEX_OBJECT_CLASS(klass) \
  G_TYPE_CHECK_CLASS_CAST(klass, DEX_TYPE_OBJECT, DexObjectClass)
#define DEX_OBJECT_GET_CLASS(obj) \
  G_TYPE_INSTANCE_GET_CLASS(obj, DEX_TYPE_OBJECT, DexObjectClass)
#define _DEX_DEFINE_TYPE(ClassName, class_name, PARENT_TYPE, Flags)                               \
  static void G_PASTE(class_name, _class_init) (G_PASTE (ClassName, Class) *);                    \
  static void G_PASTE(class_name, _init) (ClassName *);                                           \
  static gpointer G_PASTE(class_name, _parent_class);                                             \
  static GType G_PASTE(class_name, _type);                                                        \
                                                                                                  \
  static void                                                                                     \
  G_PASTE(class_name, _class_intern_init) (gpointer klass)                                        \
  {                                                                                               \
    G_PASTE (class_name, _parent_class) = g_type_class_peek_parent (klass);                       \
    G_PASTE (class_name, _class_init) ((G_PASTE (ClassName, Class) *)klass);                      \
  }                                                                                               \
                                                                                                  \
  GType G_PASTE(class_name, _get_type) (void)                                                     \
  {                                                                                               \
    if (g_once_init_enter (&G_PASTE (class_name, _type)))                                         \
      {                                                                                           \
        GType gtype = g_type_register_static_simple (                                             \
            PARENT_TYPE,                                                                          \
            g_intern_static_string (G_STRINGIFY (ClassName)),                                     \
            sizeof (G_PASTE (ClassName, Class)),                                                  \
            (GClassInitFunc) G_PASTE(class_name, _class_intern_init),                             \
            sizeof (ClassName),                                                                   \
            (GInstanceInitFunc) G_PASTE(class_name, _init),                                       \
            Flags);                                                                               \
        g_once_init_leave (&G_PASTE (class_name, _type), gtype);                                  \
      }                                                                                           \
    return G_PASTE(class_name, _type);                                                            \
  }
#define DEX_DEFINE_ABSTRACT_TYPE(ClassName, class_name, PARENT_TYPE)                              \
  _DEX_DEFINE_TYPE(ClassName, class_name, PARENT_TYPE, G_TYPE_FLAG_ABSTRACT)
#define DEX_DEFINE_FINAL_TYPE(ClassName, class_name, PARENT_TYPE)                                 \
  _DEX_DEFINE_TYPE(ClassName, class_name, PARENT_TYPE, G_TYPE_FLAG_FINAL)

typedef struct _DexWeakRef
{
  GMutex              mutex;
  struct _DexWeakRef *next;
  struct _DexWeakRef *prev;
  gpointer            mem_block;
} DexWeakRef;


void     dex_weak_ref_clear (DexWeakRef *weak_ref);
void     dex_weak_ref_init  (DexWeakRef *weak_ref,
                             gpointer    mem_block);
gpointer dex_weak_ref_get   (DexWeakRef *weak_ref);
void     dex_weak_ref_set   (DexWeakRef *weak_ref,
                             gpointer    mem_block);

typedef struct _DexObject
{
  GTypeInstance    parent_instance;
  GMutex           mutex;
  DexWeakRef      *weak_refs;
  guint            weak_refs_watermark;
  gatomicrefcount  ref_count;
} DexObject;

static inline void
dex_object_lock (gpointer data)
{
  g_mutex_lock (&DEX_OBJECT (data)->mutex);
}

static inline void
dex_object_unlock (gpointer data)
{
  g_mutex_unlock (&DEX_OBJECT (data)->mutex);
}

typedef struct _DexObjectClass
{
  GTypeClass parent_class;

  void (*finalize) (DexObject *object);
} DexObjectClass;

typedef struct _DexCallable
{
  DexObject parent_instance;
} DexCallable;

typedef struct _DexCallableClass
{
  DexObjectClass parent_class;
} DexCallableClass;

typedef struct _DexFunction
{
  DexCallable parent_class;
} DexFunction;

typedef struct _DexFunctionClass
{
  DexCallableClass parent_class;
} DexFunctionClass;

#define DEX_FUTURE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST(klass, DEX_TYPE_FUTURE, DexFutureClass))
#define DEX_FUTURE_GET_CLASS(obj) \
  G_TYPE_INSTANCE_GET_CLASS(obj, DEX_TYPE_FUTURE, DexFutureClass)

typedef struct _DexFuture
{
  DexObject parent_instance;
  GValue resolved;
  GError *rejected;
  GList *chained;
  DexFutureStatus status : 2;
} DexFuture;

typedef struct _DexFutureClass
{
  DexObjectClass parent_class;

  gboolean (*propagate) (DexFuture *future,
                         DexFuture *completed);
} DexFutureClass;

void dex_future_chain         (DexFuture    *future,
                               DexFuture    *chained);
void dex_future_complete      (DexFuture    *future,
                               const GValue *value,
                               GError       *error);
void dex_future_complete_from (DexFuture    *future,
                               DexFuture    *completed);

DexFutureSet *dex_future_set_new (DexFuture **futures,
                                  guint       n_futures,
                                  guint       n_success,
                                  gboolean    can_race,
                                  gboolean    propagate_first);

#define DEX_TYPE_BLOCK    (dex_block_get_type())
#define DEX_BLOCK(obj)    (G_TYPE_CHECK_INSTANCE_CAST(obj, DEX_TYPE_BLOCK, DexBlock))
#define DEX_IS_BLOCK(obj) (G_TYPE_CHECK_INSTANCE_TYPE(obj, DEX_TYPE_BLOCK))

typedef enum _DexBlockKind
{
  DEX_BLOCK_KIND_THEN    = 1 << 0,
  DEX_BLOCK_KIND_CATCH   = 1 << 1,
  DEX_BLOCK_KIND_FINALLY = DEX_BLOCK_KIND_THEN | DEX_BLOCK_KIND_CATCH,
} DexBlockKind;

GType      dex_block_get_type (void) G_GNUC_CONST;
DexFuture *dex_block_new      (DexFuture         *future,
                               DexBlockKind       kind,
                               DexFutureCallback  callback,
                               gpointer           callback_data,
                               GDestroyNotify     callback_data_destroy);

typedef struct _DexPromise
{
  DexFuture parent_instance;
} DexPromise;

typedef struct _DexPromiseClass
{
  DexFutureClass parent_class;
} DexPromiseClass;

typedef struct _DexTasklet
{
  DexFuture parent_instance;
} DexTasklet;

typedef struct _DexTaskletClass
{
  DexFutureClass parent_class;
} DexTaskletClass;

typedef struct _DexScheduler
{
  DexObject parent_instance;
} DexScheduler;

typedef struct _DexSchedulerClass
{
  DexObjectClass parent_class;
} DexSchedulerClass;

G_END_DECLS
