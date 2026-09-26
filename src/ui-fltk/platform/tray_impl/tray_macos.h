// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-fltk-config.h"

#if defined(SYNCSPIRIT_FLTK_MACOS)

#include "platform/tray.h"

namespace syncspirit::fltk {

struct tray_macos_t final : tray_impl_t {
    static tray_macos_t *init(app_supervisor_t &sup) noexcept;

    ~tray_macos_t();
    bool is_enabled() noexcept override;
    void set_default_icon() noexcept override;
    void set_traffic_icon() noexcept override;
    void set_offline_icon() noexcept override;

  private:
    tray_macos_t(app_supervisor_t &sup, void *native_impl);
    void *native_impl{nullptr};
};

} // namespace syncspirit::fltk

#endif