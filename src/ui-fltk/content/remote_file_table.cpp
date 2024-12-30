#include "remote_file_table.h"
#include "../tree_item/entry.h"
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
    : parent_t(x, y, w, h), container{container_}, displayed_versions{0} {
    auto host = static_cast<tree_item::entry_t *>(&container);
    auto &entry = *host->get_entry();
    auto data = table_rows_t();

    name_cell = new static_string_provider_t("");
    modified_cell = new static_string_provider_t("");
    sequence_cell = new static_string_provider_t("");
    size_cell = new static_string_provider_t("");
    block_size_cell = new static_string_provider_t("");
    blocks_cell = new static_string_provider_t("");
    permissions_cell = new static_string_provider_t("");
    modified_s_cell = new static_string_provider_t("");
    modified_ns_cell = new static_string_provider_t("");
    modified_by_cell = new static_string_provider_t("");
    symlink_target_cell = new static_string_provider_t("");
    entries_cell = new static_string_provider_t("");
    entries_size_cell = new static_string_provider_t("");

    data.push_back({"name", name_cell});
    data.push_back({"modified", modified_cell});
    data.push_back({"sequence", sequence_cell});
    data.push_back({"size", size_cell});
    data.push_back({"block size", block_size_cell});
    data.push_back({"blocks", blocks_cell});
    data.push_back({"permissions", permissions_cell});
    data.push_back({"modified_s", modified_s_cell});
    data.push_back({"modified_ns", modified_ns_cell});
    data.push_back({"modified_by", modified_by_cell});
    data.push_back({"is_directory", make_checkbox(*this, entry.is_dir())});
    data.push_back({"is_file", make_checkbox(*this, entry.is_file())});
    data.push_back({"is_link", make_checkbox(*this, entry.is_link())});
    data.push_back({"is_invalid", make_checkbox(*this, entry.is_invalid())});
    data.push_back({"is_deleted", make_checkbox(*this, entry.is_deleted())});
    data.push_back({"no_permissions", make_checkbox(*this, entry.has_no_permissions())});
    data.push_back({"symlink_target", symlink_target_cell});
    data.push_back({"entries", entries_cell});
    data.push_back({"entries size", entries_size_cell});

    assign_rows(std::move(data));

    refresh();
}

void remote_file_table_t::refresh() {
    struct size_data_t {
        std::size_t entries_count = 0;
        std::size_t entries_size = 0;
    };
    struct size_visitor_t final : tree_item::file_visitor_t {
        void visit(const model::file_info_t &file, void *data) const override {
            auto size_data = reinterpret_cast<size_data_t *>(data);
            size_data->entries_count += 1;
            size_data->entries_size += file.get_size();
        }
    };

    auto host = static_cast<tree_item::entry_t *>(&container);
    auto &entry = *host->get_entry();
    auto &devices = container.supervisor.get_cluster()->get_devices();
    auto data = table_rows_t();
    auto modified_s = entry.get_modified_s();
    auto modified_date = model::pt::from_time_t(modified_s);
    auto version = entry.get_version();

    auto size_data = size_data_t{};
    // host->apply(size_visitor_t{}, &size_data);

    name_cell->update(entry.get_name());
    modified_cell->update(model::pt::to_simple_string(modified_date));
    sequence_cell->update(fmt::format("{}", entry.get_sequence()));
    size_cell->update(fmt::format("{}", entry.get_size()));

    for (int i = 0; i < static_cast<int>(displayed_versions); ++i) {
        remove_row(4);
    }
    displayed_versions = version->counters_size();
    auto modified_by_value = entry.get_modified_by();
    auto modified_by_device = std::string_view();
    auto modified_by_device_holder = std::string();

    for (size_t i = 0; i < displayed_versions; ++i) {
        auto &counter = version->get_counter(i);
        auto modification_device = std::string_view("*unknown*");
        for (auto &it : devices) {
            auto &device = *it.item;
            auto &device_id = device.device_id();
            if (device_id.matches(counter.id())) {
                modification_device = device.get_name();
            }
            if (device_id.matches(modified_by_value)) {
                modified_by_device = device.get_name();
            }
        }
        auto value = fmt::format("({}) {}", counter.value(), modification_device);
        insert_row("modification", new static_string_provider_t(std::move(value)), 4);
    }

    if (modified_by_device.empty()) {
        modified_by_device_holder = model::device_id_t::make_short(modified_by_value);
        modified_by_device = modified_by_device_holder;
    }

    block_size_cell->update(std::to_string(entry.get_block_size()));
    blocks_cell->update(std::to_string(entry.get_blocks().size()));
    permissions_cell->update(fmt::format("0{:o}", entry.get_permissions()));
    modified_s_cell->update(fmt::format("{}", modified_s));
    modified_ns_cell->update(fmt::format("{}", entry.get_modified_ns()));
    modified_by_cell->update(fmt::format("{}", modified_by_device));
    symlink_target_cell->update(entry.get_link_target());
    entries_cell->update(fmt::format("{}", size_data.entries_count));
    entries_size_cell->update(fmt::format("{}", size_data.entries_size));

    redraw();
}
