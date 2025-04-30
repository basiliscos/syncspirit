// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "file_entity.h"
#include "local_file_presence.h"
#include "peer_file_presence.h"
#include "missing_file_presence.h"
#include "model/cluster.h"
#include "model/folder.h"
#include "model/folder_info.h"

#include <memory_resource>
#include <array>

using namespace syncspirit;
using namespace syncspirit::presentation;

file_entity_t::file_entity_t(model::file_info_t &sample_file, path_t path_) noexcept : entity_t(std::move(path_)) {
    using presence_files_t = std::pmr::vector<model::file_info_t *>;

    auto buffer = std::array<std::byte, 32 * sizeof(model::file_info_t *)>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto presence_files = presence_files_t(&pool);

    auto &fi_map = sample_file.get_folder_info()->get_folder()->get_folder_infos();
    for (auto &it_fi : fi_map) {
        auto &folder_info = it_fi.item;
        auto &files_map = folder_info->get_file_infos();
        auto file = files_map.by_name(path.get_full_name());
        if (file) {
            presence_files.emplace_back(file.get());
            has_dir = has_dir || file->is_dir();
        }
    }

    missing_file = new missing_file_presence_t(*this);
    records.emplace_back(record_t{missing_file.get()});

    for (auto &it : presence_files) {
        on_insert(*it);
    }
}

file_entity_t::~file_entity_t() {
    missing_file.reset();
    // records.clear();
}

auto file_entity_t::on_insert(model::file_info_t &file_info) noexcept
    -> std::pair<file_presence_t *, presence_stats_t> {
    auto fi = file_info.get_folder_info();
    auto device = fi->get_device();
    for (auto &[p, _] : records) {
        if (p->device == device) {
            return {static_cast<file_presence_t *>(p), {}};
        }
    }

    auto local = fi->get_folder()->get_cluster()->get_device() == device;
    auto presence = [&]() -> file_presence_t * {
        if (local) {
            return new local_file_presence_t(*this, file_info);
        } else {
            return new peer_file_presence_t(*this, file_info);
        }
    }();
    auto parent_presence = presence->set_parent(parent);
    while (parent_presence) {
        --parent_presence->entity_generation;
        parent_presence = parent_presence->parent;
    }
    records.emplace_back(record_t{presence});
    auto diff = presence->get_own_stats();
    for (auto &c : children) {
        for (auto &[p, _] : c->records) {
            if (p->device == device) {
                if (p->parent != presence) {
                    p->set_parent(presence);
                    auto child_stats = p->get_stats(false);
                    diff += child_stats;
                    presence->statistics += child_stats;
                }
            }
        }
    }
    return {presence, diff};
}
