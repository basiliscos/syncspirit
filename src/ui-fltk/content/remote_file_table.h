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
};

} // namespace syncspirit::fltk::content
