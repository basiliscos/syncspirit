// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2023-2025 Ivan Baidakou

#include "updates_streamer.h"
#include <cassert>
#include <algorithm>

using namespace syncspirit::model;

void updates_streamer_t::update_info_t::fetch() noexcept {
    assert(!fetched);
    fetched = true;
    auto it = folder_info_iterator_t(local_folder);
    it.set_min_sequence(seen_sequence + 1);
    auto file = it.next();
    while (file) {
        updated_files.push_front(file);
        file = it.next();
    }
    auto comparator = [](const file_info_t* lhs, file_info_t* rhs) -> bool {
        return lhs->get_sequence() < rhs->get_sequence();
    };
    std::sort(updated_files.begin(), updated_files.end(), comparator);
}

updates_streamer_t::updates_streamer_t(cluster_t &cluster_, device_t &device) noexcept
    : cluster{cluster_}, peer{&device} {
    refresh_remote();
}

void updates_streamer_t::refresh_remote() noexcept {
    auto &folders = cluster.get_folders();
    auto local_device = cluster.get_device().get();
    auto local_sha256 = local_device ->device_id().get_sha256();
    auto &remote_views = peer->get_remote_view_map();
    auto current_folder = std::string_view();
    if (current != -1) {
        current_folder = update_infos[current].local_folder->get_folder()->get_id();
    }
    auto updates = std::uint_fast32_t{0};
    for (auto &it : folders) {
        auto &folder = *it.item;
        if (folder.get_folder_type() != db::FolderType::receive) {
            auto local_folder = static_cast<folder_info_t*>(folder.get_presence(local_device));
            auto peer_folder = static_cast<folder_info_t*>(local_folder->get_sibling(peer.get()));
            auto predicate = [&](const update_info_t& item) {return item.local_folder == local_folder; };
            auto it_update = std::find_if( update_infos.begin(), update_infos.end(), predicate);
            auto remove_update = false;
            if (peer_folder) {
                auto remote_view = remote_views.get(local_sha256, folder.get_id());
                if (remote_view) {
                    auto index_match = remote_view->index_id == local_folder->get_index();
                    auto min_sequence = index_match ? remote_view->max_sequence : 0;
                    if (it_update != update_infos.end()) {
                        it_update->seen_sequence = min_sequence;
                        if (!index_match) {
                            it_update->seen_sequence = min_sequence;
                            it_update->updated_files.clear();
                        }
                    } else {
                        update_infos.push_back({local_folder, {}, min_sequence, false});
                        ++updates;
                    }
                } else {
                    remove_update = true;
                }
            } else {
                remove_update = true;
            }
            if (remove_update && it_update != update_infos.end()) {
                update_infos.erase(it_update);
            }
        }
    }
    if (updates) {
        current = -1;
    }
    if (current == -1) {
        if (!current_folder.empty()) {
            int index{0};
            for(auto& it_update: update_infos) {
                if (it_update.local_folder->get_folder()->get_id() == current_folder) {
                    current = index;
                    break;
                }
                ++index;
            }
        }
        if (current == -1 && !update_infos.empty()) {
            current  = 0;
        }
    }
}

auto updates_streamer_t::next() noexcept -> update_t {
    auto iterations = std::size_t{0};
    if (current != -1) {
        while (iterations < update_infos.size()) {
            auto& update = update_infos[current];
            auto& [folder_info, updated_files, seen_sequence, fetched] = update_infos[current];
            if (!fetched) {
                update.fetch();
            }
            if (!updated_files.empty()) {
                auto initial = seen_sequence == 0;
                if (initial) {
                    ++seen_sequence;
                }
                auto fi = updated_files.front();
                updated_files.pop_front();
                return {fi, folder_info.get(), initial};
            }
            current = (current + 1) % static_cast<int>(update_infos.size());
            ++iterations;
        }
        current = -1;
    }
    return {{}, {}, false};
}

bool updates_streamer_t::on_update(file_info_t &file, const folder_info_t &fi) noexcept {
    for (auto& update: update_infos) {
        if (update.local_folder == &fi) {
            if (update.fetched) {
                update.updated_files.push_back(&file);
                if (current == -1) {
                    current = 0;
                }
                return true;
            }
            break;
        }
    }
    return false;
}
