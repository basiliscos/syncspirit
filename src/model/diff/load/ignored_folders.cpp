// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "ignored_folders.h"
#include "model/cluster.h"

using namespace syncspirit::model::diff::load;

auto ignored_folders_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept
    -> outcome::result<void> {
    auto &map = cluster.get_ignored_folders();
    for (auto &pair : folders) {
        auto key_ptr = reinterpret_cast<const char*>(pair.key.data());
        auto label_ptr = reinterpret_cast<const char*>(pair.value.data());
        auto folder_id = std::string_view(key_ptr, pair.key.size());
        auto label = std::string_view(label_ptr, pair.value.size());
        auto option = ignored_folder_t::create(folder_id, label);
        if (!option) {
            return option.assume_error();
        }
        auto &folder = option.value();
        map.put(folder);
    }
    return applicator_t::apply_sibling(cluster, controller);
};
