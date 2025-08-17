// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "remove_folder_infos.h"
#include "reset_folder_infos.h"
#include "model/cluster.h"
#include "model/diff/apply_controller.h"
#include "model/diff/cluster_visitor.h"
#include "model/misc/file_iterator.h"
#include <algorithm>

using namespace syncspirit::model::diff::modify;

remove_folder_infos_t::remove_folder_infos_t(const uuid_folder_infos_map_t &map, orphaned_blocks_t *orphaned_blocks_) {
    auto keys = unique_keys_t();
    for (auto &it : map) {
        auto &folder_info = *it.second;
        auto key = folder_info.get_key();
        keys.emplace(utils::bytes_t(key.data(), key.data() + key.size()));
    }
    std::move(keys.begin(), keys.end(), std::back_inserter(this->keys));
    assign_child(new reset_folder_infos_t(map, orphaned_blocks_));
    LOG_DEBUG(log, "remove_folder_infos_t, count = {}", map.size());
}

auto remove_folder_infos_t::apply_impl(apply_controller_t &controller, void *custom) const noexcept
    -> outcome::result<void> {
    auto r = applicator_t::apply_child(controller, custom);
    if (!r) {
        return r;
    }
    auto &cluster = controller.get_cluster();
    for (auto &key : keys) {
        auto decomposed = folder_info_t::decompose_key(key);
        auto folder = cluster.get_folders().by_key(decomposed.folder_key());
        auto &folder_infos = folder->get_folder_infos();
        auto device_key = decomposed.device_key();
        auto folder_info = folder_infos.by_device_key(device_key);
        if (auto iterator = folder_info->get_device()->get_iterator(); iterator) {
            if (folder_info->get_device() != cluster.get_device()) {
                iterator->on_remove(folder_info);
            }
        }

        folder_infos.remove(folder_info);
    }
    return applicator_t::apply_sibling(controller, custom);
}

auto remove_folder_infos_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting remove_folder_infos_t");
    return visitor(*this, custom);
}
