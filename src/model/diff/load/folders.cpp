// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "folders.h"
#include "model/cluster.h"
#include "model/misc/error_code.h"
#include "proto/proto-helpers.h"

using namespace syncspirit::model::diff::load;

auto folders_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept -> outcome::result<void> {
    auto &map = cluster.get_folders();
    for (auto &pair : folders) {

        auto db_folder = db::Folder();
        if (auto ok = db::decode::decode(pair.value, db_folder); !ok) {
            return make_error_code(error_code_t::folder_deserialization_failure);
        }
        auto option = folder_t::create(pair.key, db_folder);
        if (!option) {
            return option.assume_error();
        }
        auto &folder = option.value();
        map.put(folder);
        folder->assign_cluster(&cluster);
    }
    return applicator_t::apply_sibling(cluster, controller);
}
