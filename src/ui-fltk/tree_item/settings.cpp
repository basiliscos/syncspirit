#include "settings.h"
#include "../config/control.h"
#include <spdlog/fmt/fmt.h>

using namespace syncspirit::fltk::tree_item;

settings_t::settings_t(app_supervisor_t &supervisor, Fl_Tree *tree) : parent_t(supervisor, tree, false) {
    update_label(false);
}

bool settings_t::on_select() {
    supervisor.replace_content([&](Fl_Widget *prev) -> Fl_Widget * {
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
