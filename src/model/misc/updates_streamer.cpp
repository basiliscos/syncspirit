// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2023 Ivan Baidakou

#include "updates_streamer.h"

using namespace syncspirit::model;

updates_streamer_t::updates_streamer_t(cluster_t &cluster, device_t &device) noexcept : peer{&device} {
    auto &folders = cluster.get_folders();
    for (auto &it : folders) {
        auto &folder = *it.item;
        auto folder_info = folder.is_shared_with(device);
        if (folder_info) {
            auto local = folder.get_folder_infos().by_device(cluster.get_device());
            folders_queue.insert(local);
        }
    }
    prepare();
}

updates_streamer_t::operator bool() const noexcept {
    if (folders_queue.empty()) {
    }
    return false;
}

void updates_streamer_t::prepare() noexcept {}
