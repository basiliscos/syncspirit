// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "callback.h"

using namespace syncspirit::fltk;

callback_t::callback_t(callback_fn_t fn_) noexcept : fn{std::move(fn_)} {}

void callback_t::eval() noexcept {
    if (fn) {
        fn();
    }
}
