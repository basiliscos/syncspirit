// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "tray_base.h"
#include "app_supervisor.h"
#include "syncspirit-fltk-config.h"

#if defined(SYNCSPIRIT_FLTK_X11)
#include "tray_impl/tray_x11.h"
#elif defined(SYNCSPIRIT_FLTK_WIN32)
#include "tray_impl/tray_win32.h"
#endif

#include <FL/Fl_PNG_Image.H>

using namespace syncspirit::fltk;

static const char *traffic_icon_path = "icons/syncspirit-fltk-sync.png";

static void cb_quit(Fl_Widget *, void *data) {
    auto tray_widget = reinterpret_cast<tray_impl_t *>(data);
    auto &sup = tray_widget->sup;
    sup.get_logger()->info("exiting via menu");
    sup.do_shutdown();
}

tray_impl_t::tray_impl_t(app_supervisor_t &sup_) noexcept : sup{sup_} {
    menu_items.push_back({"Quit", 0, cb_quit, this, 0, 0, 0, 14, 0});
    menu_items.push_back({nullptr});

    auto buffer = std::array<std::byte, 1024 * 32>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto icon_path = sup.resolve_resource(allocator, traffic_icon_path);
    if (!icon_path.empty()) {
        traffic_image.reset(new Fl_PNG_Image(icon_path.get_full_name().data()));
        if (!(traffic_image->w() && traffic_image->h())) {
            traffic_image.reset();
        }
    }
}

tray_base_t::~tray_base_t() {
    if (impl) {
        delete impl;
    }
}

void tray_base_t::init(app_supervisor_t &sup_) noexcept { sup = &sup_; }

void tray_base_t::enable(bool value) noexcept {
    if (value) {
        if (impl) {
            delete impl;
            impl = nullptr;
        }
#if defined(SYNCSPIRIT_FLTK_X11)
        impl = tray_x11_t::init(*sup);
#elif defined(SYNCSPIRIT_FLTK_WIN32)
        impl = tray_win32_t::init(*sup);
#endif
    } else {
        delete impl;
        impl = nullptr;
    }
}

bool tray_base_t::is_enabled() noexcept {
    if (impl) {
        return impl->is_enabled();
    }
    return false;
}

void tray_base_t::on_frame_render() noexcept {
    if (impl && impl->is_enabled()) {
        auto &self = *sup->get_cluster()->get_device();
        auto new_traffic = (self.get_rx_bytes() + self.get_tx_bytes()) << 1;
        if (new_traffic != traffic) {
            traffic = (traffic & 1) ? new_traffic : new_traffic | 1;
        }
        if (traffic & 1) {
            impl->set_traffic_icon();
        } else {
            impl->set_default_icon();
        }
    }
}

bool tray_base_t::is_available() noexcept {
#if defined(SYNCSPIRIT_FLTK_X11)
    return true;
#elif defined(SYNCSPIRIT_FLTK_WIN32)
    return true;
#endif
    return false;
}
