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

using F = presence_t::features_t;

file_entity_t::file_entity_t(model::path_ptr_t path_, const model::folder_infos_map_t &fi_map) noexcept
    : entity_t(std::move(path_)) {
    using pair_t = std::pair<model::file_info_t *, model::folder_info_t *>;
    using presence_files_t = std::pmr::vector<pair_t>;

    auto buffer = std::array<std::byte, 32 * sizeof(model::file_info_t *)>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto presence_files = presence_files_t(&pool);

    for (auto &it_fi : fi_map) {
        auto &folder_info = *it_fi.item;
        auto &files_map = folder_info.get_file_infos();
        auto file = files_map.by_name(path->get_full_name());
        if (file) {
            presence_files.emplace_back(pair_t(file.get(), &folder_info));
        }
    }

    missing_file = new missing_file_presence_t(*this);
    presences.emplace_back(missing_file.get());

    for (auto &[file, folder_info] : presence_files) {
        on_insert(*file, *folder_info);
    }
}

auto file_entity_t::on_insert(model::file_info_t &file_info, const model::folder_info_t &fi) noexcept
    -> file_presence_t * {
    auto device = fi.get_device();
    for (auto p : presences) {
        if (p->device == device) {
            return static_cast<file_presence_t *>(p);
        }
    }

    auto is_local = fi.get_folder()->get_cluster()->get_device() == device;
    auto presence = [&]() -> file_presence_t * {
        if (is_local) {
            return new local_file_presence_t(*this, file_info, fi);
        } else {
            return new peer_file_presence_t(*this, file_info, fi);
        }
    }();
    presences.emplace_back(presence);
    auto parent_presence = presence->set_parent(parent);
    if (parent_presence) {
        parent_presence->clear_children();
    }
    while (parent_presence) {
        --parent_presence->entity_generation;
        parent_presence = parent_presence->parent;
    }
    return presence;
}
