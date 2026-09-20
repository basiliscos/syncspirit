// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-fltk-config.h"

#if defined(SYNCSPIRIT_FLTK_MACOS)

#include "platform/window_controller.h"

namespace syncspirit::fltk {

struct window_controller_impl_macos_t final : window_controller_impl_t {
    using parent_t = window_controller_impl_t;
    window_controller_impl_macos_t(main_window_t *) noexcept;

    void show() noexcept override;
    void hide() noexcept override;
    bool native_hidden{false};
};

} // namespace syncspirit::fltk

#endif
