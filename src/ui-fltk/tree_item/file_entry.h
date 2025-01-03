#pragma once

#include "entry.h"

namespace syncspirit::fltk::tree_item {


struct file_entry_t : entry_t {
    using parent_t = entry_t;
    using parent_t::parent_t;
#if 0
    file_entry_t(app_supervisor_t &supervisor, Fl_Tree *tree, augmentation_entry_t& augmentation);

    void update_label() override;
    void on_update() override;
    void on_open() override;
    bool on_select() override;

    dynamic_item_t* create(augmentation_entry_t&) override;
    bool expanded;
#endif
};

}; // namespace syncspirit::fltk::tree_item
