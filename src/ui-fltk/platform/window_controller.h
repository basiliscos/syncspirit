// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include <memory>

namespace syncspirit::fltk {

struct main_window_t;

struct window_controller_impl_t {
    window_controller_impl_t(main_window_t *) noexcept;
    virtual ~window_controller_impl_t() = default;

    virtual void show() noexcept;
    virtual void hide() noexcept;

  protected:
    main_window_t *main_window;
};

struct window_controller_t {
    window_controller_t(main_window_t *) noexcept;

    void show() noexcept;
    void hide() noexcept;

  private:
    using impl_t = std::unique_ptr<window_controller_impl_t>;
    impl_t impl;
};

} // namespace syncspirit::fltk
