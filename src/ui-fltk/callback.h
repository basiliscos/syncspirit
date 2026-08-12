// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include <functional>
#include "model/misc/arc.hpp"

namespace syncspirit::fltk {

using callback_fn_t = std::function<void()>;

struct callback_t final : model::arc_base_t<callback_t> {
    callback_t(callback_fn_t fn) noexcept;
    void eval() noexcept;
    callback_fn_t fn;
};

using callback_ptr_t = model::intrusive_ptr_t<callback_t>;

} // namespace syncspirit::fltk
