// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "menu.h"
#include "platform/tray.h"
#include "app_supervisor.h"

using namespace syncspirit::fltk;

#define SS_INT_TO_PTR(X) reinterpret_cast<void *>(static_cast<std::intptr_t>(X + 1))
#define SS_PTR_TO_INT(X) static_cast<int>(reinterpret_cast<std::intptr_t>(X))

static void on_quit(Fl_Widget *widget, void *) {
    auto &sup = static_cast<menu_t *>(widget)->supervisor;
    auto log = sup.get_logger();
    LOG_INFO(log, "quit via menu");
    sup.do_shutdown();
}

static void on_colorize(Fl_Widget *widget, void *data) {
    auto menu = static_cast<menu_t *>(widget);
    auto item = menu->find_item_with_user_data(data);
    auto value = item->value() ? true : false;
    menu->supervisor.set_show_colorized(value);
}

static void on_show_deleted(Fl_Widget *widget, void *data) {
    auto menu = static_cast<menu_t *>(widget);
    auto item = menu->find_item_with_user_data(data);
    auto value = item->value() ? true : false;
    menu->supervisor.set_show_deleted(value);
}

static void on_show_missing(Fl_Widget *widget, void *data) {
    auto menu = static_cast<menu_t *>(widget);
    auto item = menu->find_item_with_user_data(data);
    auto value = item->value() ? true : false;
    menu->supervisor.set_show_missing(value);
}

static void on_display_tray(Fl_Widget *widget, void *data) {
    auto menu = static_cast<menu_t *>(widget);
    auto item = menu->find_item_with_user_data(data);
    auto value = item->value() ? true : false;
    menu->supervisor.set_tray_display(value);
}

static void on_hide_to_tray(Fl_Widget *widget, void *data) {
    auto menu = static_cast<menu_t *>(widget);
    auto item = menu->find_item_with_user_data(data);
    auto value = item->value() ? true : false;
    menu->supervisor.set_hide_to_tray(value);
}

menu_t::menu_t(app_supervisor_t &supervisor_, int x, int y, int w, int h)
    : supervisor{supervisor_}, parent_t(x, y, w, h) {
    int index = 0;
    index = add("&File", 0, 0, 0, FL_SUBMENU);
    index = add("&File/&Quit", 0, on_quit);

    index = add("&Options", 0, 0, 0, FL_SUBMENU);
    index = add("&Options/Tree", 0, 0, 0, FL_SUBMENU);
    index = [&]() {
        bool value = supervisor.get_app_config().fltk_config.display_colorized;
        auto flags = FL_MENU_TOGGLE | (value ? FL_MENU_VALUE : 0);
        return add("&Options/Tree/Colorize", 0, on_colorize, SS_INT_TO_PTR(index), flags);
    }();
    index = [&]() {
        bool value = supervisor.get_app_config().fltk_config.display_deleted;
        auto flags = FL_MENU_TOGGLE | (value ? FL_MENU_VALUE : 0);
        return add("&Options/Tree/Show Deleted", 0, on_show_deleted, SS_INT_TO_PTR(index), flags);
    }();
    index = [&]() {
        bool value = supervisor.get_app_config().fltk_config.display_missing;
        auto flags = FL_MENU_TOGGLE | (value ? FL_MENU_VALUE : 0);
        return add("&Options/Tree/Show Missing", 0, on_show_missing, SS_INT_TO_PTR(index), flags);
    }();

    index = add("&Options/Tray", 0, 0, 0, FL_SUBMENU);
    index = [&]() {
        bool value = supervisor.get_app_config().fltk_config.display_tray_icon;
        auto flags = FL_MENU_TOGGLE | (value ? FL_MENU_VALUE : 0) | (tray_t::is_available() ? 0 : FL_MENU_INACTIVE);
        return add("&Options/Tray/Enable", 0, on_display_tray, SS_INT_TO_PTR(index), flags);
    }();
    index = [&]() {
        bool value = supervisor.get_app_config().fltk_config.hide_to_tray;
        auto flags = FL_MENU_TOGGLE | (value ? FL_MENU_VALUE : 0) | (tray_t::is_available() ? 0 : FL_MENU_INACTIVE);
        return add("&Options/Tray/Hide on close", 0, on_hide_to_tray, SS_INT_TO_PTR(index), flags);
    }();
    // index = add("&Options/Tray/Enable", 0, 0, 0, FL_MENU_TOGGLE);
    // index = add("&Options/Tray/Hide on close", 0, on_quit);
}
