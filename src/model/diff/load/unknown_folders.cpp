// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "unknown_folders.h"
#include "../../cluster.h"
#include "../../misc/error_code.h"

using namespace syncspirit::model::diff::load;

auto unknown_folders_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto &items = cluster.get_unknown_folders();
    for (auto &pair : folders) {
        auto data = pair.value;
        auto db = db::UnknownFolder();
        auto ok = db.ParseFromArray(data.data(), data.size());
        if (!ok) {
            return make_error_code(error_code_t::unknown_folder_deserialization_failure);
        }

        auto option = unknown_folder_t::create(pair.key, db);
        if (!option) {
            return option.assume_error();
        }
        auto &folder = option.value();
        items.put(std::move(folder));
    }
    return outcome::success();
}
