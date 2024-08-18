#pragma once

#include "../tree_item.h"

namespace syncspirit::fltk::tree_item {

struct folders_t final : tree_item_t {
    using parent_t = tree_item_t;
    folders_t(app_supervisor_t &supervisor, Fl_Tree *tree);

    void select_folder(std::string_view folder_id);
    augmentation_ptr_t add_folder(model::folder_t &folder);
    void update_label() override;

    bool on_select() override;
    void remove_child(tree_item_t *child) override;
};

} // namespace syncspirit::fltk::tree_item
