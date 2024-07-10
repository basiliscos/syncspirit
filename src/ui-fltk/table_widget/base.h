#pragma once

#include "../static_table.h"
#include "../tree_item.h"

namespace syncspirit::fltk::table_widget {

struct base_t : widgetable_t {
    using parent_t = widgetable_t;

    base_t(tree_item_t &container);

    Fl_Widget *get_widget() override;
    virtual void reset();
    virtual bool store(void *);

    tree_item_t &container;
    Fl_Widget *widget = nullptr;
};

using table_widget_ptr_t = boost::intrusive_ptr<base_t>;

} // namespace syncspirit::fltk::table_widget
