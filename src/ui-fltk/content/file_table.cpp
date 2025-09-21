// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "file_table.h"

#include "table_widget/checkbox.h"
#include "utils.hpp"
#include "proto/proto-helpers-bep.h"
#include "presentation/cluster_file_presence.h"

#include <memory_resource>
#include <algorithm>

using namespace syncspirit::presentation;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::content;

using F = presence_t::features_t;

static constexpr size_t max_history_records = 5;

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

static presence_t *resolve_presence(presence_t *source) {
    if (source->get_features() & F::missing) {
        auto best = source->get_entity()->get_best();
        return const_cast<presence_t *>(best);
    }
    return source;
}

file_table_t::file_table_t(presence_item_t &container_, int x, int y, int w, int h)
    : parent_t(x, y, w, h), container{container_}, displayed_versions{0} {

    auto presence = resolve_presence(&container.get_presence());
    assert(presence->get_features() & F::cluster);
    auto cluster_presence = static_cast<cluster_file_presence_t *>(presence);
    auto &entity = cluster_presence->get_file_info();
    auto data = table_rows_t();

    name_cell = new static_string_provider_t("");
    device_cell = new static_string_provider_t("");
    modified_cell = new static_string_provider_t("");
    sequence_cell = new static_string_provider_t("");
    size_cell = new static_string_provider_t("");
    block_size_cell = new static_string_provider_t("");
    blocks_cell = new static_string_provider_t("");
    permissions_cell = new static_string_provider_t("");
    modified_s_cell = new static_string_provider_t("");
    modified_ns_cell = new static_string_provider_t("");
    symlink_target_cell = new static_string_provider_t("");
    entries_cell = new static_string_provider_t("");
    entries_size_cell = new static_string_provider_t("");
    local_entries_cell = new static_string_provider_t("");

    data.push_back({"name", name_cell});
    data.push_back({"device", device_cell});
    data.push_back({"modified", modified_cell});
    data.push_back({"sequence", sequence_cell});
    data.push_back({"size", size_cell});
    data.push_back({"block size", block_size_cell});
    data.push_back({"blocks", blocks_cell});
    data.push_back({"permissions", permissions_cell});
    data.push_back({"modified_s", modified_s_cell});
    data.push_back({"modified_ns", modified_ns_cell});
    data.push_back({"is_directory", make_checkbox(*this, entity.is_dir())});
    data.push_back({"is_file", make_checkbox(*this, entity.is_file())});
    data.push_back({"is_link", make_checkbox(*this, entity.is_link())});
    data.push_back({"is_invalid", make_checkbox(*this, entity.is_invalid())});
    data.push_back({"is_deleted", make_checkbox(*this, entity.is_deleted())});
    data.push_back({"no_permissions", make_checkbox(*this, entity.has_no_permissions())});
    data.push_back({"symlink_target", symlink_target_cell});
    data.push_back({"entries", entries_cell});
    data.push_back({"entries size", entries_size_cell});
    data.push_back({"cluster/local/avail", local_entries_cell});

    assign_rows(std::move(data));

    refresh();
}

void file_table_t::refresh() {
#if 0
    using allocator_t = std::pmr::polymorphic_allocator<char>;
    using counters_t = std::pmr::vector<proto::Counter>;
    auto buffer = std::array<std::byte, 1024>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = allocator_t(&pool);

    auto presence = resolve_presence(&container.get_presence());
    assert(presence->get_features() & F::cluster);
    auto cluster_presence = static_cast<cluster_file_presence_t *>(presence);
    auto &file_info = cluster_presence->get_file_info();
    auto device = file_info.get_folder_info()->get_device();
    auto &devices = container.supervisor.get_cluster()->get_devices();
    auto data = table_rows_t();
    auto modified_s = file_info.get_modified_s();
    auto modified_date = model::pt::from_time_t(modified_s);
    auto version = file_info.get_version();
    auto &stats = presence->get_stats();

    name_cell->update(file_info.get_name()->get_full_name());
    device_cell->update(fmt::format("{} ({})", device->get_name(), device->device_id().get_short()));
    modified_cell->update(model::pt::to_simple_string(modified_date));
    sequence_cell->update(fmt::format("{}", file_info.get_sequence()));
    size_cell->update(get_file_size(file_info.get_size()));

    for (int i = 0; i < static_cast<int>(displayed_versions); ++i) {
        remove_row(5);
    }

    auto &model_counters = version.get_counters();
    auto sorted_counters = counters_t(model_counters.begin(), model_counters.end(), allocator);
    auto sorter = [](const proto::Counter &lhs, const proto::Counter &rhs) -> bool {
        return proto::get_value(lhs) > proto::get_value(rhs);
    };
    std::sort(sorted_counters.begin(), sorted_counters.end(), sorter);

    displayed_versions = std::min(version.counters_size(), max_history_records);
    for (size_t i = 0; i < displayed_versions; ++i) {
        auto &counter = sorted_counters[i];
        auto value = proto::get_value(counter);
        auto device_id = proto::get_id(counter);
        auto device_short = model::device_id_t::make_short(device_id);
        auto device_str = std::pmr::string(allocator);
        for (auto &it : devices) {
            auto &peer = *it.item;
            auto &peer_id = peer.device_id();
            if (peer.device_id().get_uint() == device_id) {
                fmt::format_to(std::back_inserter(device_str), "{}, {}", peer.get_name(), device_short);
                break;
            }
        }
        if (device_str.empty()) {
            device_str = std::pmr::string(device_short, allocator);
        }

        auto label = std::pmr::string(allocator);
        fmt::format_to(std::back_inserter(label), "({}) {}", value, device_str);
        insert_row("modification", new static_string_provider_t(std::move(label)), 5 + i);
    }

    block_size_cell->update(std::to_string(file_info.get_block_size()));
    blocks_cell->update(std::to_string(file_info.get_blocks().size()));
    permissions_cell->update(fmt::format("0{:o}", file_info.get_permissions()));
    modified_s_cell->update(fmt::format("{}", modified_s));
    modified_ns_cell->update(fmt::format("{}", file_info.get_modified_ns()));
    symlink_target_cell->update(file_info.get_link_target());
    entries_cell->update(fmt::format("{}", stats.entities));
    entries_size_cell->update(get_file_size(stats.size));

    auto entries_stats = fmt::format("{}/{}/{}", stats.cluster_entries, stats.entities, stats.local_entries);
    local_entries_cell->update(entries_stats);

    redraw();
#endif
    std::abort();
}
