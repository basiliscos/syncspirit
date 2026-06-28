// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

namespace syncspirit::fltk {

struct app_supervisor_t;
struct main_window_t;

struct tray_impl_t {
    virtual ~tray_impl_t() = default;
    virtual bool is_enabled() noexcept = 0;

  protected:
    app_supervisor_t *sup = nullptr;
};

struct tray_base_t {
    ~tray_base_t();
    void init(app_supervisor_t &sup) noexcept;
    void enable(bool value) noexcept;
    bool is_enabled() noexcept;
    static bool is_available() noexcept;

  protected:
    app_supervisor_t *sup = nullptr;
    tray_impl_t *impl = nullptr;
};

} // namespace syncspirit::fltk
