#include "settings.h"

using namespace syncspirit::fltk::tree_item;

settings_t::settings_t(app_supervisor_t &supervisor, Fl_Tree *tree) : parent_t(supervisor, tree) { label("settings"); }

void settings_t::on_select() {}
