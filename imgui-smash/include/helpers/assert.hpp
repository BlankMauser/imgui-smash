/*
 * Copyright (c) Atmosphère-NX
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "logger/Logger.hpp"
#include "nn/diag.h"

// Adapted from exlaunch's abort/assert

#define SMASH_ABORT_IMPL(cond, value, ...)  ({ \
    ::Logger::log(value, ## __VA_ARGS__);         \
    ::nn::diag::detail::AbortImpl(__FILE__, __func__, value, __LINE__);   \
});

#define SMASH_ABORT(value, ...) SMASH_ABORT_IMPL("", value, ## __VA_ARGS__)

#define SMASH_ASSERT_IMPL(expr, ...)                                                                            \
    ({                                                                                                        \
        if (!(static_cast<bool>(expr))) {                                                                                  \
      SMASH_ABORT(#expr, ## __VA_ARGS__);                                                 \
        }                                                                                                     \
    })

#define SMASH_ASSERT(expr, ...) SMASH_ASSERT_IMPL(expr, ## __VA_ARGS__)