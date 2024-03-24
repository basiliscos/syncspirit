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
            auto remote_folder = remote_folders.by_folder(*folder);
            if (remote_folder && remote_folder->needs_update()) {
                folders_queue.insert(remote_folder);
            }
        }
    }
    prepare();
}

updates_streamer_t &updates_streamer_t::operator=(updates_streamer_t &&other) noexcept {
    peer = std::move(other.peer);
    folders_queue = std::move(other.folders_queue);
    files_queue = std::move(other.files_queue);
    return *this;
}

updates_streamer_t::operator bool() const noexcept { return !files_queue.empty() || !folders_queue.empty(); }

auto updates_streamer_t::next() noexcept -> file_info_ptr_t {
    auto &by_sequence = files_queue.template get<1>();
    auto it = by_sequence.begin();
    auto r = *it;
    by_sequence.erase(it);
    prepare();
    return r;
}

void updates_streamer_t::prepare() noexcept {
    while (true) {
        if (!files_queue.empty()) {
            return;
        }
        if (folders_queue.empty()) {
            return;
        }
        auto it = folders_queue.begin();
        auto remote_folder = *it;
        folders_queue.erase(it);

        auto local_folder = remote_folder->get_local();
        auto &files = local_folder->get_file_infos();
        auto sequence_threshold =
            remote_folder->get_index() == local_folder->get_index() ? remote_folder->get_max_sequence() : 0;

        for (auto fi : files) {
            auto &file = fi.item;
            if (file->get_sequence() > sequence_threshold) {
                files_queue.emplace(file);
            }
        }
    }
}

void updates_streamer_t::on_update(file_info_t &file) noexcept {
    if (!peer) {
        return;
    }

    auto folder = file.get_folder_info()->get_folder();
    auto remote_folder = peer->get_remote_folder_infos().by_folder(*folder);
    if (!remote_folder) {
        return;
    }

    if (folders_queue.count(remote_folder)) {
        return;
    }

    auto &by_uuid = files_queue.template get<0>();
    auto it = by_uuid.find(file.get_uuid());
    if (it != by_uuid.end()) {
        by_uuid.erase(it);
    }

    files_queue.emplace(&file);
}
