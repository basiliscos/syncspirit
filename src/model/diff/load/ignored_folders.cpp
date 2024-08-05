// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "ignored_folders.h"
#include "../../cluster.h"

using namespace syncspirit::model::diff::load;

auto ignored_folders_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto &map = cluster.get_ignored_folders();
    for (auto &pair : folders) {
        auto option = ignored_folder_t::create(pair.key, pair.value);
        if (!option) {
            return option.assume_error();
        }
        auto &folder = option.value();
        map.put(folder);
    }
    return next ? next->apply(cluster) : outcome::success();
}
