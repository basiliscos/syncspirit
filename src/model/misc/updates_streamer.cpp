// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2023 Ivan Baidakou

#include "updates_streamer.h"
#include "model/remote_folder_info.h"

using namespace syncspirit::model;

updates_streamer_t::updates_streamer_t(cluster_t &cluster, device_t &device) noexcept : peer{&device} {
    auto &folders = cluster.get_folders();
    auto &remote_folders = device.get_remote_folder_infos();
    for (auto &it : folders) {
        auto &folder = it.item;
        auto folder_info = folder->is_shared_with(device);
        if (folder_info) {
            auto remote_folder = remote_folders.by_folder(folder);
            if (remote_folder && remote_folder->needs_update()) {
                folders_queue.insert(remote_folder);
            }
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
