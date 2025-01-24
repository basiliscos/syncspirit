// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "toolbar.h"
#include "symbols.h"

#include <FL/Fl_Toggle_Button.H>

using namespace syncspirit::fltk;

static constexpr int padding = 5;
static constexpr int button_h = 20;

static void set_show_deleted(Fl_Widget *widget, void *data) {
    auto toolbar = reinterpret_cast<toolbar_t *>(data);
    auto button = static_cast<Fl_Toggle_Button *>(widget);
    auto value = button->value() ? true : false;
    toolbar->supervisor.set_show_deleted(value);
}

static void set_show_colorized(Fl_Widget *widget, void *data) {
    auto toolbar = reinterpret_cast<toolbar_t *>(data);
    auto button = static_cast<Fl_Toggle_Button *>(widget);
    auto value = button->value() ? true : false;
    toolbar->supervisor.set_show_colorized(value);
}

toolbar_t::toolbar_t(app_supervisor_t &supervisor_, int x, int y, int w, int)
    : parent_t(x, y, w, button_h + padding * 2), supervisor{supervisor_} {
    auto ww = 40 - padding * 2;
    auto xx = x + padding;
    auto yy = y + padding;

    [&]() {
        auto button = new Fl_Toggle_Button(xx, yy, ww, button_h, symbols::deleted.data());
        button->tooltip("show deleted");
        button->callback(set_show_deleted, this);
        bool value = supervisor.get_app_config().fltk_config.display_deleted;
        button->value(value ? 1 : 0);
        xx += ww + padding;
    }();

    [&]() {
        auto button = new Fl_Toggle_Button(xx, yy, ww, button_h, symbols::colorize.data());
        button->tooltip("show colorized");
        button->callback(set_show_colorized, this);
        bool value = supervisor.get_app_config().fltk_config.display_colorized;
        button->value(value ? 1 : 0);
    }();

    end();
    resizable(nullptr);
}
