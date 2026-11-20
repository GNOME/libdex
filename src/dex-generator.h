/* dex-generator.h
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

#include "dex-channel.h"
#include "dex-coroutine.h"
#include "dex-fiber.h"
#include "dex-tuple.h"

G_BEGIN_DECLS

#define DEX_TYPE_GENERATOR (dex_generator_get_type())
#define DEX_GENERATOR(obj) (G_TYPE_CHECK_INSTANCE_CAST (obj, DEX_TYPE_GENERATOR, DexGenerator))
#define DEX_IS_GENERATOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE (obj, DEX_TYPE_GENERATOR))

typedef struct _DexGenerator DexGenerator;

/**
 * DexGeneratorFunc:
 * @user_data: callback data
 *
 * Produces the next future in the calling context, without observing it.
 *
 * Returns: (transfer full) (nullable): an item, or %NULL for permanent exhaustion
 * Since: 1.3
 */
typedef DexFuture *(*DexGeneratorFunc) (gpointer user_data);

/**
 * DexGeneratorMapFunc:
 * @value: (transfer none): the borrowed input value
 * @result: (out caller-allocates): an initially uninitialized output value
 * @user_data: callback data
 *
 * Initializes @result with the mapped value. The adapter unsets it after
 * yielding a copy.
 *
 * Since: 1.3
 */
typedef void (*DexGeneratorMapFunc) (const GValue *value,
                                     GValue       *result,
                                     gpointer      user_data);

/**
 * DexGeneratorFilterFunc:
 * @value: (transfer none): the borrowed input value
 * @user_data: callback data
 *
 * Returns: whether to keep the input value
 * Since: 1.3
 */
typedef gboolean (*DexGeneratorFilterFunc) (const GValue *value,
                                            gpointer      user_data);

DEX_AVAILABLE_IN_1_3
gboolean      dex_generator_await_next         (DexGenerator            *self,
                                                GValue                  *value,
                                                GError                 **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean      dex_generator_await_next_boolean (DexGenerator            *self,
                                                gboolean                *value,
                                                GError                 **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean      dex_generator_await_next_boxed   (DexGenerator            *self,
                                                GType                    expected_type,
                                                gpointer                *value,
                                                GError                 **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean      dex_generator_await_next_double  (DexGenerator            *self,
                                                double                  *value,
                                                GError                 **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean      dex_generator_await_next_enum    (DexGenerator            *self,
                                                GType                    expected_type,
                                                int                     *value,
                                                GError                 **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean      dex_generator_await_next_fd      (DexGenerator            *self,
                                                int                     *value,
                                                GError                 **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean      dex_generator_await_next_flags   (DexGenerator            *self,
                                                GType                    expected_type,
                                                guint                   *value,
                                                GError                 **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean      dex_generator_await_next_float   (DexGenerator            *self,
                                                float                   *value,
                                                GError                 **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean      dex_generator_await_next_future  (DexGenerator            *self,
                                                DexFuture              **value,
                                                GError                 **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean      dex_generator_await_next_int     (DexGenerator            *self,
                                                int                     *value,
                                                GError                 **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean      dex_generator_await_next_int64   (DexGenerator            *self,
                                                gint64                  *value,
                                                GError                 **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean      dex_generator_await_next_object  (DexGenerator            *self,
                                                GObject                **value,
                                                GError                 **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean      dex_generator_await_next_pointer (DexGenerator            *self,
                                                gpointer                *value,
                                                GError                 **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean      dex_generator_await_next_string  (DexGenerator            *self,
                                                char                   **value,
                                                GError                 **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean      dex_generator_await_next_uint    (DexGenerator            *self,
                                                guint                   *value,
                                                GError                 **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean      dex_generator_await_next_uint64  (DexGenerator            *self,
                                                guint64                 *value,
                                                GError                 **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
gboolean      dex_generator_await_next_variant (DexGenerator            *self,
                                                GVariant               **value,
                                                GError                 **error) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
void          dex_generator_close              (DexGenerator            *self);
DEX_AVAILABLE_IN_1_3
DexFuture    *dex_generator_close_after_drain  (DexGenerator            *self) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexGenerator *dex_generator_filter             (DexGenerator            *source,
                                                DexGeneratorFilterFunc   callback,
                                                gpointer                 user_data,
                                                GDestroyNotify           destroy) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
GType         dex_generator_get_type           (void);
DEX_AVAILABLE_IN_1_3
gboolean      dex_generator_is_exhausted       (DexGenerator            *self);
DEX_AVAILABLE_IN_1_3
DexGenerator *dex_generator_map                (DexGenerator            *source,
                                                DexGeneratorMapFunc      callback,
                                                gpointer                 user_data,
                                                GDestroyNotify           destroy) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexGenerator *dex_generator_new                (DexGeneratorFunc         producer,
                                                gpointer                 user_data,
                                                GDestroyNotify           destroy) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexGenerator *dex_generator_new_coroutine      (DexScheduler            *scheduler,
                                                DexCoroutineFunc         func,
                                                gpointer                 user_data,
                                                GDestroyNotify           destroy) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexGenerator *dex_generator_new_for_channel    (DexChannel              *channel,
                                                DexFuture               *completion) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexGenerator *dex_generator_new_fiber          (DexScheduler            *scheduler,
                                                gsize                    stack_size,
                                                DexFiberFunc             func,
                                                gpointer                 user_data,
                                                GDestroyNotify           destroy) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexFuture    *dex_generator_next               (DexGenerator            *self) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexGenerator *dex_generator_zip                (DexGenerator            *first,
                                                ...) G_GNUC_WARN_UNUSED_RESULT;
DEX_AVAILABLE_IN_1_3
DexGenerator *dex_generator_zipv               (DexGenerator * const    *generators,
                                                guint                    n_generators) G_GNUC_WARN_UNUSED_RESULT;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (DexGenerator, dex_unref)

G_END_DECLS
