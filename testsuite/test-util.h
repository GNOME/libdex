/* test-util.h
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <libdex.h>

G_BEGIN_DECLS

static inline DexFuture *
_dex_test_runner_fiber (gpointer user_data)
{
  GTestFunc func = (GTestFunc) G_CALLBACK (user_data);

  func ();
  return dex_future_new_true ();
}

static inline DexFuture *
_dex_test_runner_quit (DexFuture *future,
                       gpointer   user_data)
{
  g_main_loop_quit (user_data);
  return dex_ref (future);
}

static inline void
_dex_test_runner (gconstpointer user_data)
{
  GMainLoop *main_loop = g_main_loop_new (NULL, FALSE);

  dex_init ();

  dex_future_disown (dex_future_finally (dex_scheduler_spawn (NULL,
                                                              8 * 1024 * 1024,
                                                              _dex_test_runner_fiber,
                                                              (gpointer) user_data,
                                                              NULL),
                                         _dex_test_runner_quit,
                                         g_main_loop_ref (main_loop),
                                         (GDestroyNotify) g_main_loop_unref));

  g_main_loop_run (main_loop);
  g_main_loop_unref (main_loop);
}

static inline void
_g_test_add_func (const char *path,
                  GTestFunc   test_func)
{
  g_test_add_data_func (path, test_func, _dex_test_runner);
}

G_END_DECLS
