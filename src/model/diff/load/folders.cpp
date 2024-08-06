// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "folders.h"
#include "../../cluster.h"
#include "../../misc/error_code.h"

using namespace syncspirit::model::diff::load;

auto folders_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto &map = cluster.get_folders();
    for (auto &pair : folders) {
        auto data = pair.value;
        auto db = db::Folder();
        auto ok = db.ParseFromArray(data.data(), data.size());
        if (!ok) {
            return make_error_code(error_code_t::folder_deserialization_failure);
        }

        auto option = folder_t::create(pair.key, db);
        if (!option) {
            return option.assume_error();
        }
        auto &folder = option.value();
        map.put(folder);
        folder->assign_cluster(&cluster);
    }
    return applicator_t::apply_sibling(cluster);
}
