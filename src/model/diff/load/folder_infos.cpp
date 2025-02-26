// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "folder_infos.h"
#include "model/misc/error_code.h"
#include "model/cluster.h"
#include "proto/proto-helpers.h"

using namespace syncspirit::model::diff::load;

auto folder_infos_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept
    -> outcome::result<void> {
    auto &folders = cluster.get_folders();
    auto &devices = cluster.get_devices();
    for (auto &pair : container) {
        auto &key = pair.key;
        auto decomposed = folder_info_t::decompose_key(key);
        auto device_key = decomposed.device_key();
        auto device = devices.by_key(device_key);
        if (!device) {
            return make_error_code(error_code_t::no_such_device);
        }

        auto folder_key = decomposed.folder_key();
        auto folder = folders.by_key(folder_key);
        if (!folder) {
            return make_error_code(error_code_t::no_such_folder);
        }

        auto db_fi = db::FolderInfo();
        if (auto ok = db::decode::decode(pair.value, db_fi); !ok) {
            return make_error_code(error_code_t::folder_info_deserialization_failure);
        }

        auto &map = folder->get_folder_infos();
        auto option = folder_info_t::create(key, db_fi, device, folder);
        if (!option) {
            return option.assume_error();
        }
        auto &fi = option.assume_value();
        map.put(fi);
    }
    return applicator_t::apply_sibling(cluster, controller);
}
