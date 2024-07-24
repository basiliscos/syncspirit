#pragma once

#include "../static_table.h"
#include "../tree_item.h"

#include <boost/smart_ptr/local_shared_ptr.hpp>

namespace syncspirit::fltk::content {

struct folder_table_t : static_table_t {
    using parent_t = static_table_t;
    using shared_devices_t = boost::local_shared_ptr<model::devices_map_t>;
    struct device_share_widget_t;

    folder_table_t(tree_item_t &container_, shared_devices_t shared_with_, shared_devices_t non_shared_with_,
                   table_rows_t &&rows, int x, int y, int w, int h);

  private:
    bool on_remove_share(widgetable_t &widget, model::device_ptr_t device, model::device_ptr_t initial);
    void on_select(model::device_ptr_t device, model::device_ptr_t previous);
    void on_add_share(widgetable_t &widget);
    std::pair<int, int> scan(widgetable_t &widget);

    model::devices_map_t initially_shared_with;
    model::devices_map_t initially_non_shared_with;
    shared_devices_t shared_with;
    shared_devices_t non_shared_with;
    tree_item_t &container;

    friend class device_share_widget_t;
};

} // namespace syncspirit::fltk::content
