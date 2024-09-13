#include "peer_dir.h"

using namespace syncspirit::fltk::tree_item;

peer_dir_t::peer_dir_t(app_supervisor_t &supervisor, Fl_Tree *tree, model::file_info_t &entry_)
    : parent_t(supervisor, tree, true), entry{entry_} {
    entry.set_augmentation(get_proxy());
}

auto peer_dir_t::get_entry() -> model::file_info_t * { return &entry; }
