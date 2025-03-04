// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "settings.h"
#include "../config/control.h"
#include <spdlog/fmt/fmt.h>

using namespace syncspirit::fltk::tree_item;

settings_t::settings_t(app_supervisor_t &supervisor, Fl_Tree *tree) : parent_t(supervisor, tree, false) {
    update_label(false);
}

bool settings_t::on_select() {
    content = supervisor.replace_content([&](content_t *content) -> content_t * {
        auto prev = content->get_widget();
        content = new config::control_t(*this, prev->x(), prev->y(), prev->w(), prev->h());
        return content;
    });
    return true;
}

void settings_t::update_label(bool modified) {
    auto l = std::string("settings");
    if (modified) {
        l = fmt::format("({}) {}", "*", l);
    }
    label(l.data());
    tree()->redraw();
}
