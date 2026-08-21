// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include <vector>
#include <cstdint>
#include <FL/Fl_Menu_Item.H>
#include <memory>

namespace syncspirit::fltk {

struct app_supervisor_t;
struct main_window_t;

struct tray_impl_t {
    using menu_items_t = std::vector<Fl_Menu_Item>;
    using image_icon_t = std::unique_ptr<Fl_RGB_Image>;

    tray_impl_t(app_supervisor_t &sup) noexcept;
    virtual ~tray_impl_t() = default;
    virtual bool is_enabled() noexcept = 0;
    virtual void set_default_icon() noexcept = 0;
    virtual void set_traffic_icon() noexcept = 0;
    virtual void set_offline_icon() noexcept = 0;

    app_supervisor_t &sup;
    std::uint64_t traffic{0};
    menu_items_t menu_items;
    image_icon_t traffic_image;
    image_icon_t offline_image;
};

struct tray_t {
    ~tray_t();
    void init(app_supervisor_t &sup) noexcept;
    void enable(bool value) noexcept;
    bool is_enabled() const noexcept;
    static bool is_available() noexcept;
    void on_frame_render() noexcept;
    void on_local_state_update() noexcept;

  protected:
    app_supervisor_t *sup = nullptr;
    tray_impl_t *impl = nullptr;
    std::uint64_t traffic{0};
};

} // namespace syncspirit::fltk
