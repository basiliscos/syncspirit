// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "remove_folder_infos.h"

#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"
#include "db/prefix.h"

using namespace syncspirit::model::diff::modify;

auto remove_folder_infos_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    for (auto &key : keys) {
        auto decomposed = folder_info_t::decompose_key(key);
        char folder_key_data[folder_t::data_length];
        folder_key_data[0] = (char)(db::prefix::folder);
        std::copy(decomposed.folder_uuid.begin(), decomposed.folder_uuid.end(), folder_key_data + 1);
        auto folder_key = std::string_view(folder_key_data, folder_t::data_length);
        auto folder = cluster.get_folders().get(folder_key);
        auto &folder_infos = folder->get_folder_infos();
        auto folder_info = folder_infos.by_device_id(decomposed.device_id);
        folder_infos.remove(folder_info);
    }
    return outcome::success();
}

auto remove_folder_infos_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting remove_folder_infos_t");
    return visitor(*this, custom);
}
