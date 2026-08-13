// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "menu.h"
#include "platform/tray.h"
#include "app_supervisor.h"

using namespace syncspirit;
using namespace syncspirit::fltk;

#define SS_INT_TO_PTR(X) reinterpret_cast<void *>(static_cast<std::intptr_t>((X) + 1))
#define SS_PTR_TO_INT(X) static_cast<int>(reinterpret_cast<std::intptr_t>((X)))

#define SS_ID_NET_STOP SS_INT_TO_PTR(-2)
#define SS_ID_NET_START SS_INT_TO_PTR(-3)

static void on_quit(Fl_Widget *widget, void *) {
    auto &sup = static_cast<menu_t *>(widget)->supervisor;
    auto log = sup.get_logger();
    LOG_INFO(log, "quit via menu");
    sup.do_shutdown();
}

static void on_net_start(Fl_Widget *widget, void *) {
    auto &sup = static_cast<menu_t *>(widget)->supervisor;
    auto log = sup.get_logger();
    LOG_INFO(log, "starting networking");
    sup.send_model<net::payload::start_services_t>();
}

static void on_net_stop(Fl_Widget *widget, void *) {
    auto &sup = static_cast<menu_t *>(widget)->supervisor;
    auto log = sup.get_logger();
    LOG_INFO(log, "stopping networking");
    sup.send_model<net::payload::stop_services_t>();
}

static void on_net_restart(Fl_Widget *widget, void *) {
    auto &sup = static_cast<menu_t *>(widget)->supervisor;
    auto log = sup.get_logger();
    LOG_INFO(log, "restarting networking");
    sup.send_model<net::payload::restart_services_t>();
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

static void on_show_folder_id(Fl_Widget *widget, void *data) {
    auto menu = static_cast<menu_t *>(widget);
    auto item = menu->find_item_with_user_data(data);
    auto value = item->value() ? true : false;
    menu->supervisor.set_show_folder_id(value);
}

static void on_show_device_id(Fl_Widget *widget, void *data) {
    auto menu = static_cast<menu_t *>(widget);
    auto item = menu->find_item_with_user_data(data);
    auto value = item->value() ? true : false;
    menu->supervisor.set_show_device_id(value);
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
    index = add_item("&File", 0, 0, 0, FL_SUBMENU);
    index = add_item("Network", 0, 0, 0, FL_SUBMENU);
    index = add_item("Stop", 0, on_net_stop, SS_ID_NET_STOP);
    index = add_item("Start", 0, on_net_start, SS_ID_NET_START);
    index = add_item("Restart", 0, on_net_restart);
    index = finish_submenu(); // Network
    index = add_item("&Quit", 0, on_quit);
    index = finish_submenu(); // File

    index = add_item("&Options", 0, 0, 0, FL_SUBMENU);
    index = add_item("Tree", 0, 0, 0, FL_SUBMENU);
    index = [&]() {
        bool value = supervisor.get_app_config().fltk_config.display_colorized;
        auto flags = FL_MENU_TOGGLE | (value ? FL_MENU_VALUE : 0);
        return add_item("Colorize", 0, on_colorize, SS_INT_TO_PTR(index), flags);
    }();
    index = [&]() {
        bool value = supervisor.get_app_config().fltk_config.display_deleted;
        auto flags = FL_MENU_TOGGLE | (value ? FL_MENU_VALUE : 0);
        return add_item("Show Deleted", 0, on_show_deleted, SS_INT_TO_PTR(index), flags);
    }();
    index = [&]() {
        bool value = supervisor.get_app_config().fltk_config.display_missing;
        auto flags = FL_MENU_TOGGLE | (value ? FL_MENU_VALUE : 0);
        return add_item("Show Missing", 0, on_show_missing, SS_INT_TO_PTR(index), flags);
    }();
    index = [&]() {
        bool value = supervisor.get_app_config().fltk_config.display_folder_id;
        auto flags = FL_MENU_TOGGLE | (value ? FL_MENU_VALUE : 0);
        return add_item("Show folder id", 0, on_show_folder_id, SS_INT_TO_PTR(index), flags);
    }();
    index = [&]() {
        bool value = supervisor.get_app_config().fltk_config.display_device_id;
        auto flags = FL_MENU_TOGGLE | (value ? FL_MENU_VALUE : 0);
        return add_item("Show device id", 0, on_show_device_id, SS_INT_TO_PTR(index), flags);
    }();
    index = finish_submenu(); // Options/Tree

    index = add_item("Tray", 0, 0, 0, FL_SUBMENU);
    index = [&]() {
        bool value = supervisor.get_app_config().fltk_config.display_tray_icon;
        auto flags = FL_MENU_TOGGLE | (value ? FL_MENU_VALUE : 0) | (tray_t::is_available() ? 0 : FL_MENU_INACTIVE);
        return add_item("Enable", 0, on_display_tray, SS_INT_TO_PTR(index), flags);
    }();
    index = [&]() {
        bool value = supervisor.get_app_config().fltk_config.hide_to_tray;
        auto flags = FL_MENU_TOGGLE | (value ? FL_MENU_VALUE : 0) | (tray_t::is_available() ? 0 : FL_MENU_INACTIVE);
        return add_item("Hide on close", 0, on_hide_to_tray, SS_INT_TO_PTR(index), flags);
    }();
    index = finish_submenu(); // Options/Tray
    index = finish_submenu(); // Options

    finish_submenu(); // whole menu

    on_local_state_update();

    menu(items.data());
}

int menu_t::add_item(const char *label, int shortcut, Fl_Callback *cb, void *user_data, int flags) noexcept {
    items.emplace_back(label, shortcut, cb, user_data, flags);
    return static_cast<int>(items.size());
}

int menu_t::finish_submenu() noexcept { return add_item(nullptr, 0, nullptr); }

void menu_t::on_local_state_update() noexcept {
    using S = model::connection_state_t;
    auto cluster = supervisor.get_cluster();
    if (cluster) {
        auto new_state = cluster->get_device()->get_state().get_connection_state();
        auto flags_stop = false;
        auto flags_start = false;
        if (new_state == S::offline) {
            flags_start = true;
        } else {
            flags_stop = true;
        }
        for (auto &item : items) {
            if (item.user_data() == SS_ID_NET_STOP) {
                if (flags_stop) {
                    item.flags &= ~FL_MENU_INACTIVE;
                } else {
                    item.flags |= FL_MENU_INACTIVE;
                }
            } else if (item.user_data() == SS_ID_NET_START) {
                if (flags_start) {
                    item.flags = item.flags & ~FL_MENU_INACTIVE;
                } else {
                    item.flags = item.flags | FL_MENU_INACTIVE;
                }
            }
        }
        update();
        redraw();
    }
}
