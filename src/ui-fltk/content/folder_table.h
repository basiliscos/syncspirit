#pragma once

#include "../static_table.h"
#include "../tree_item.h"

#include <boost/smart_ptr/local_shared_ptr.hpp>

namespace syncspirit::fltk::content {

struct folder_table_t : static_table_t {
    using parent_t = static_table_t;
    using shared_devices_t = boost::local_shared_ptr<model::devices_map_t>;

    enum class mode_t { share, edit };

    struct serialiazation_context_t {
        db::Folder folder;
        db::FolderInfo folder_info;
        model::devices_map_t shared_with;
    };

    struct folder_description_t {
        model::folder_data_t folder_data;
        std::size_t entries;
        std::uint64_t index;
        std::int64_t max_sequence;
        shared_devices_t shared_with;
        shared_devices_t non_shared_with;
    };

    folder_table_t(tree_item_t &container_, const folder_description_t &description, mode_t mode, int x, int y, int w,
                   int h);

    bool on_remove_share(widgetable_t &widget, model::device_ptr_t device, model::device_ptr_t initial);
    void on_select(model::device_ptr_t device, model::device_ptr_t previous);
    void on_add_share(widgetable_t &widget);
    std::pair<int, int> scan(widgetable_t &widget);

    void on_remove();
    void on_apply();
    void on_share();
    void on_reset();
    void on_rescan();

    model::folder_data_t folder_data;
    mode_t mode;
    std::size_t entries;
    std::uint64_t index;
    std::int64_t max_sequence;
    model::devices_map_t initially_shared_with;
    model::devices_map_t initially_non_shared_with;
    shared_devices_t shared_with;
    shared_devices_t non_shared_with;
    tree_item_t &container;
    Fl_Widget *apply_button;
    Fl_Widget *share_button;
    Fl_Widget *reset_button;
};

} // namespace syncspirit::fltk::content
