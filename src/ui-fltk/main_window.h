// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2026 Ivan Baidakou

#pragma once

#include "app_supervisor.h"
#include "platform/tray.h"
#include "platform/window_controller.h"
#include <FL/Fl_Double_Window.H>
#include <string>

namespace syncspirit::fltk {

struct log_panel_t;
struct tree_view_t;
struct menu_t;

struct main_window_t : Fl_Double_Window {
    using parent_t = Fl_Double_Window;
    using parent_t::show;

    main_window_t(app_supervisor_t &supervisor, int w, int h);
    ~main_window_t();

    void on_shutdown();
    void set_splash_text(std::string text);
    void on_loading_done();
    void detach_supervisor();
    const Fl_RGB_Image *get_icon() const noexcept;
    app_supervisor_t *get_supervisor();

    int handle(int e) override;
    void hide() override;
    void show() override;
    void show_tray_icon(bool value) noexcept;
    void on_frame_render() noexcept;
    void on_local_state_update() noexcept;
    tray_t &get_tray() noexcept;

  private:
    app_supervisor_t *supervisor{nullptr};
    Fl_Group *content_left{nullptr};
    tree_view_t *tree{nullptr};
    log_panel_t *log_panel{nullptr};
    Fl_RGB_Image *image_icon{nullptr};
    menu_t *menu{nullptr};
    tray_t tray;
    window_controller_t controller;
};

} // namespace syncspirit::fltk
