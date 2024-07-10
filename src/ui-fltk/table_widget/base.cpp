#include "base.h"

using namespace syncspirit::fltk::table_widget;

base_t::base_t(tree_item_t &container_) : container{container_}, widget{nullptr} {}

Fl_Widget *base_t::get_widget() { return widget; }

void base_t::reset() {}

bool base_t::store(void *) { return true; }
