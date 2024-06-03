#pragma once

#include "model/device.h"
#include "../tree_item.h"
#include "../static_table.h"
#include <vector>

namespace syncspirit::fltk::tree_item {

struct peer_device_t : tree_item_t {
    using parent_t = tree_item_t;

    struct peer_widget_t : widgetable_t {
        using parent_t = widgetable_t;
        using parent_t::parent_t;

        Fl_Widget *get_widget() override;
        Fl_Widget *widget = nullptr;
    };

    using peer_widget_ptr_t = boost::intrusive_ptr<peer_widget_t>;

    using widgets_t = std::vector<peer_widget_ptr_t>;
    peer_device_t(model::device_ptr_t peer, app_supervisor_t &supervisor, Fl_Tree *tree);

    const model::device_t &get_device() const;
    void on_select() override;
    widgetable_ptr_t record(peer_widget_ptr_t);

    model::device_ptr_t peer;
    Fl_Widget *table;
    widgets_t widgets;
};

} // namespace syncspirit::fltk::tree_item
