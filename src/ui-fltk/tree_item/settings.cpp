#include "settings.h"
#include "../config/control.h"

using namespace syncspirit::fltk::tree_item;

settings_t::settings_t(app_supervisor_t &supervisor, Fl_Tree *tree) : parent_t(supervisor, tree) { label("settings"); }

void settings_t::on_select() {
    supervisor.replace_content([&](Fl_Widget *prev) -> Fl_Widget * {
        auto control = new config::control_t(*this, prev->x(), prev->y(), prev->w(), prev->h());
        return control;
    });
}
