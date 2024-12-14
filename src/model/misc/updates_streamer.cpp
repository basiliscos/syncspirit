// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2023-2024 Ivan Baidakou

#include "updates_streamer.h"
#include "model/remote_folder_info.h"
#include <algorithm>
#include <spdlog/spdlog.h>

using namespace syncspirit::model;

updates_streamer_t::updates_streamer_t(cluster_t &cluster_, device_t &device) noexcept
    : cluster{cluster_}, peer{&device}, self{cluster.get_device()} {
    refresh_remote();
}

void updates_streamer_t::refresh_remote() noexcept {
    auto &folders = cluster.get_folders();
    auto &remote_folders = peer->get_remote_folder_infos();
    auto streaming_folder = (model::folder_info_t *)(nullptr);
    auto prev_seen = std::move(seen_info);
    seen_info = {};
    for (auto &it : folders) {
        auto &folder = it.item;
        auto peer_folder = folder->is_shared_with(*peer);
        if (peer_folder) {
            auto local_folder = folder->get_folder_infos().by_device(*self);
            if (streaming && streaming->folder_info == local_folder) {
                streaming_folder = local_folder.get();
            }
            auto remote_folder = remote_folders.by_folder(*folder);
            if (remote_folder) {
                auto seen_sequence = std::int64_t{0};
                if (remote_folder->get_index() == local_folder->get_index()) {
                    auto previously_seen = prev_seen[local_folder];
                    seen_sequence = std::max(remote_folder->get_max_sequence(), previously_seen);
                }
                seen_info[local_folder] = seen_sequence;
            }
        }
    }
    if (streaming && !streaming_folder) {
        streaming.reset();
    } else if (streaming_folder) {
        auto remote_folder = remote_folders.by_folder(*streaming_folder->get_folder());
        if (!remote_folder) {
            streaming.reset();
        } else {
            // TODO, refresh files?
        }
    }
}

bool updates_streamer_t::on_update(file_info_t &file) noexcept {
    assert(file.get_folder_info()->get_device() == self);
    bool r = false;
    auto fi = file.get_folder_info();
    if (!streaming) {
        for (auto &[folder_info, seen_sequence] : seen_info) {
            if (folder_info.get() == fi) {
                auto max = folder_info->get_max_sequence();
                auto unseen_files = file_infos_map_t();
                auto [it, end] = folder_info->get_file_infos().range(seen_sequence + 1, max);
                for (; it != end; ++it) {
                    auto &f = it->item;
                    unseen_files.put(f.get());
                }
                streaming = streaming_info_t(folder_info, std::move(unseen_files));
                r = true;
                break;
            }
        }
    } else if (streaming) {
        auto &info = *streaming;
        if (info.folder_info.get() == file.get_folder_info()) {
            info.unseen_files.put(&file);
            r = true;
        }
    }
    return r;
}

void updates_streamer_t::on_remote_refresh() noexcept { refresh_remote(); }

file_info_ptr_t updates_streamer_t::next() noexcept {
    if (streaming) {
        auto &files = streaming->unseen_files;
        auto &proj = files.sequence_projection();
        if (!proj.empty()) {
            auto it = proj.begin();
            auto file = it->item;
            files.remove(file);
            seen_info[streaming->folder_info] = file->get_sequence();
            return file;
        }
        streaming.reset();
    }
    for (auto &[folder_info, seen_sequence] : seen_info) {
        auto max = folder_info->get_max_sequence();
        if (seen_sequence < max) {
            auto [it, end] = folder_info->get_file_infos().range(seen_sequence + 1, max);
            if (it != end) {
                auto file = it->item;
                seen_info[folder_info] = file->get_sequence();
                ++it;
                if (it != end) {
                    auto unseen_files = file_infos_map_t();
                    for (; it != end; ++it) {
                        auto &f = it->item;
                        unseen_files.put(f.get());
                    }
                    streaming = streaming_info_t(folder_info, std::move(unseen_files));
                }
                return file;
            }
        }
    }
    return {};
}
