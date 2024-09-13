#include "peer_entry.h"

using namespace syncspirit::fltk::tree_item;

peer_entry_t::peer_entry_t(app_supervisor_t &supervisor, Fl_Tree *tree, model::file_info_t &entry_)
    : parent_t(supervisor, tree, true), entry{entry_} {
    entry.set_augmentation(get_proxy());
}

auto peer_entry_t::get_entry() -> model::file_info_t * { return &entry; }
