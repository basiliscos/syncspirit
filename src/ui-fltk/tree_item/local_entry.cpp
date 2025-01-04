#include "local_entry.h"

using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

auto local_entry_t::create_child(augmentation_entry_t &entry) -> entry_t * {
    return new local_entry_t(supervisor, tree(), &entry, this);
}
