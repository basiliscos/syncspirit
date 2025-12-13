// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2023-2025 Ivan Baidakou

#include "updates_streamer.h"
#include <algorithm>
#include <spdlog/spdlog.h>

using namespace syncspirit::model;

updates_streamer_t::updates_streamer_t(cluster_t &cluster_, device_t &device) noexcept
    : cluster{cluster_}, peer{&device}, self{cluster.get_device()} {
    refresh_remote();
}

void updates_streamer_t::refresh_remote() noexcept {
    auto &folders = cluster.get_folders();
    auto &remote_views = peer->get_remote_view_map();
    auto streaming_folder = (model::folder_info_t *)(nullptr);
    auto prev_seen = std::move(seen_info);
    seen_info = {};
    for (auto &it : folders) {
        auto &folder = *it.item;
        if (folder.get_folder_type() != db::FolderType::receive) {
            auto peer_folder = folder.is_shared_with(*peer);
            if (peer_folder) {
                auto local_folder = folder.get_folder_infos().by_device(*self);
                if (streaming && streaming->folder_info == local_folder) {
                    streaming_folder = local_folder.get();
                }
                auto remote_view = remote_views.get(*cluster.get_device(), folder);
                if (remote_view) {
                    auto index_match = remote_view->index_id == local_folder->get_index();
                    auto remote_max = index_match ? remote_view->max_sequence : 0;
                    auto previously_seen = prev_seen[local_folder];
                    auto seen = std::max(remote_max, previously_seen);
                    seen_info[local_folder] = seen;
                }
            }
        }
    }
    if (streaming && !streaming_folder) {
        streaming.reset();
    } else if (streaming_folder) {
        auto &folder = *streaming_folder->get_folder();
        auto remote_view = remote_views.get(*cluster.get_device(), folder);
        if (remote_view) {
            if (remote_view->index_id != streaming_folder->get_index()) {
                streaming.reset();
            }
        } else {
            // TODO, refresh files?
        }
    }
}

bool updates_streamer_t::on_update(file_info_t &file, const folder_info_t &fi) noexcept {
    assert(fi.get_device() == self);
    bool r = false;
    if (!streaming) {
        for (auto &[folder_info, seen_sequence] : seen_info) {
            if (folder_info.get() == &fi) {
                auto max = folder_info->get_max_sequence();
                auto unseen_files = file_infos_map_t();
                auto [it, end] = folder_info->get_file_infos().range(seen_sequence + 1, max);
                for (; it != end; ++it) {
                    auto &f = *it;
                    unseen_files.put(f.get());
                }
                streaming = streaming_info_t(folder_info, std::move(unseen_files));
                r = true;
                break;
            }
        }
    } else if (streaming) {
        auto &info = *streaming;
        if (info.folder_info.get() == &fi) {
            info.unseen_files.put(&file);
            r = true;
        }
    }
    return r;
}

void updates_streamer_t::on_remote_refresh() noexcept { refresh_remote(); }

auto updates_streamer_t::next() noexcept -> update_t {
    if (streaming) {
        auto &files = streaming->unseen_files;
        auto &proj = files.sequence_projection();
        if (!proj.empty()) {
            auto file = *proj.begin();
            files.remove(file);
            auto &seen_sequence = seen_info[streaming->folder_info];
            auto initial = seen_sequence == 0;
            seen_sequence = file->get_sequence();
            return {file, streaming->folder_info.get(), initial};
        }
        streaming.reset();
    }
    for (auto &[folder_info, seen_sequence] : seen_info) {
        auto max = folder_info->get_max_sequence();
        if (seen_sequence < max) {
            auto [it, end] = folder_info->get_file_infos().range(seen_sequence + 1, max);
            if (it != end) {
                auto initial = seen_sequence == 0;
                auto &file = *it;
                seen_info[folder_info] = file->get_sequence();
                ++it;
                if (it != end) {
                    auto unseen_files = file_infos_map_t();
                    for (; it != end; ++it) {
                        auto &f = *it;
                        unseen_files.put(f.get());
                    }
                    streaming = streaming_info_t(folder_info, std::move(unseen_files));
                }
                return {file, folder_info.get(), initial};
            }
        }
    }
    return {{}, {}, false};
}
