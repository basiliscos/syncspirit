#pragma once

#include "../static_table.h"
#include "../tree_item.h"

namespace syncspirit::fltk::content {

struct remote_file_table_t : static_table_t {
    using parent_t = static_table_t;

    remote_file_table_t(tree_item_t &container_, int x, int y, int w, int h);

    void refresh() override;

  private:
    tree_item_t &container;
    static_string_provider_ptr_t name_cell;
    static_string_provider_ptr_t modified_cell;
    static_string_provider_ptr_t sequence_cell;
    static_string_provider_ptr_t size_cell;
    static_string_provider_ptr_t block_size_cell;
    static_string_provider_ptr_t blocks_cell;
    static_string_provider_ptr_t permissions_cell;
    static_string_provider_ptr_t modified_s_cell;
    static_string_provider_ptr_t modified_ns_cell;
    static_string_provider_ptr_t modified_by_cell;
    static_string_provider_ptr_t symlink_target_cell;
    static_string_provider_ptr_t entries_cell;
    static_string_provider_ptr_t entries_size_cell;
    int top_modifitcation;
};

} // namespace syncspirit::fltk::content
