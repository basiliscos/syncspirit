#pragma once

#include "../static_table.h"
#include "../tree_item.h"

#include <boost/smart_ptr/local_shared_ptr.hpp>

namespace syncspirit::fltk::content {

struct folder_table_t : static_table_t {
    using parent_t = static_table_t;
    using shared_devices_t = boost::local_shared_ptr<model::devices_map_t>;

    struct serialiazation_context_t {
        db::Folder folder;
        std::uint64_t index;
        model::devices_map_t shared_with;
    };

    static widgetable_ptr_t make_title(folder_table_t &container, std::string_view title);
    static widgetable_ptr_t make_path(folder_table_t &container, bool disabled);
    static widgetable_ptr_t make_id(folder_table_t &container, bool disabled);
    static widgetable_ptr_t make_label(folder_table_t &container);
    static widgetable_ptr_t make_folder_type(folder_table_t &container);
    static widgetable_ptr_t make_pull_order(folder_table_t &container);
    static widgetable_ptr_t make_index(folder_table_t &container, bool disabled);
    static widgetable_ptr_t make_read_only(folder_table_t &container);
    static widgetable_ptr_t make_rescan_interval(folder_table_t &container);
    static widgetable_ptr_t make_ignore_permissions(folder_table_t &container);
    static widgetable_ptr_t make_ignore_delete(folder_table_t &container);
    static widgetable_ptr_t make_disable_tmp(folder_table_t &container);
    static widgetable_ptr_t make_paused(folder_table_t &container);
    static widgetable_ptr_t make_shared_with(folder_table_t &container, model::device_ptr_t device, bool disabled);
    static widgetable_ptr_t make_notice(folder_table_t &container);

    folder_table_t(tree_item_t &container_, const model::folder_info_t &description, int x, int y, int w, int h);

    // void refresh() override;

    bool on_remove_share(widgetable_t &widget, model::device_ptr_t device, model::device_ptr_t initial);
    void on_select(model::device_ptr_t device, model::device_ptr_t previous);
    void on_add_share(widgetable_t &widget);
    std::pair<int, int> scan(widgetable_t &widget);

    void on_remove();
    void on_apply();
    void on_share();
    void on_reset();
    void on_rescan();

    const model::folder_info_t &description;
    model::devices_map_t initially_shared_with;
    model::devices_map_t initially_non_shared_with;
    shared_devices_t shared_with;
    shared_devices_t non_shared_with;
    tree_item_t &container;
    std::string error;
    widgetable_ptr_t notice;
    static_string_provider_ptr_t entries_cell;
    static_string_provider_ptr_t entries_size_cell;
    static_string_provider_ptr_t max_sequence_cell;
    static_string_provider_ptr_t scan_start_cell;
    static_string_provider_ptr_t scan_finish_cell;
    Fl_Widget *apply_button;
    Fl_Widget *share_button;
    Fl_Widget *reset_button;
    Fl_Widget *rescan_button;
};

} // namespace syncspirit::fltk::content
