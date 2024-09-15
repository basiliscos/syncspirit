#include "remote_file_table.h"
#include "../tree_item/virtual_entry.h"
#include "../table_widget/checkbox.h"

using namespace syncspirit::fltk;
using namespace syncspirit::fltk::content;

static constexpr int max_history_records = 5;

namespace {

struct ro_checkbox_t : table_widget::checkbox_t {
    using parent_t = table_widget::checkbox_t;

    ro_checkbox_t(Fl_Widget &container, bool value_) : parent_t(container), value{value_} {}

    Fl_Widget *create_widget(int x, int y, int w, int h) override {
        auto widget = parent_t::create_widget(x, y, w, h);
        input->deactivate();
        input->value(value ? 1 : 0);
        return widget;
    }

    bool value;
};

auto make_checkbox(Fl_Widget &container, bool value) -> widgetable_ptr_t { return new ro_checkbox_t(container, value); }

} // namespace

remote_file_table_t::remote_file_table_t(tree_item_t &container_, int x, int y, int w, int h)
    : parent_t(x, y, w, h), container{container_} {
    assert(dynamic_cast<tree_item::virtual_entry_t *>(&container));
    refresh();
}

void remote_file_table_t::refresh() {
    auto &entry = *dynamic_cast<tree_item::virtual_entry_t *>(&container)->get_entry();
    auto &devices = container.supervisor.get_cluster()->get_devices();
    auto data = table_rows_t();
    auto modified_s = entry.get_modified_s();
    auto modified_date = model::pt::from_time_t(modified_s);
    auto modified = model::pt::to_simple_string(modified_date);
    auto &version = entry.get_version();
    auto v_from = version.counters_size() - 1;
    auto v_to = std::max(0, v_from - max_history_records);

    data.push_back({"name", std::string(entry.get_name())});
    data.push_back({"modified", modified});
    data.push_back({"sequence", fmt::format("{}", entry.get_sequence())});
    data.push_back({"size", std::to_string(entry.get_size())});

    for (int i = v_from; i >= v_to; --i) {
        auto &counter = version.counters(i);
        auto modification_device = std::string_view("*unknown*");
        for (auto &it : devices) {
            if (it.item->matches(counter.id())) {
                modification_device = it.item->get_name();
            }
        }
        data.push_back({"modification", fmt::format("({}) {}", counter.value(), modification_device)});
    }

    data.push_back({"block size", std::to_string(entry.get_block_size())});
    data.push_back({"blocks", std::to_string(entry.get_blocks().size())});
    data.push_back({"permissions", fmt::format("0{:o}", entry.get_permissions())});
    data.push_back({"modified_s", fmt::format("{}", modified_s)});
    data.push_back({"modified_ns", fmt::format("{}", entry.get_modified_ns())});
    data.push_back({"modified_by", fmt::format("{}", entry.get_modified_by())});
    data.push_back({"is_link", make_checkbox(*this, entry.is_link())});
    data.push_back({"is_invalid", make_checkbox(*this, entry.is_invalid())});
    data.push_back({"no_permissions", make_checkbox(*this, entry.has_no_permissions())});
    data.push_back({"symlink_target", std::string(entry.get_link_target())});
    assign_rows(std::move(data));
}
