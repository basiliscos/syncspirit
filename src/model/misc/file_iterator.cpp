// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "file_iterator.h"
#include "model/cluster.h"
#include "model/misc/version_utils.h"

using namespace syncspirit::model;

file_iterator_t::guard_t::guard_t(model::file_info_ptr_t file_, file_iterator_t &owner_) noexcept
    : file{file_}, owner{owner_} {
    assert(!file->is_locally_locked());
    file->locally_lock();

    is_locked = file->get_size() > 0;
}

file_iterator_t::guard_t::~guard_t() { file->locally_unlock(); }

file_iterator_t::file_iterator_t(cluster_t &cluster_, const device_ptr_t &peer_) noexcept
    : cluster{cluster_}, peer{peer_.get()}, folder_index{0} {
    auto &folders = cluster.get_folders();
    for (auto &[folder, _] : folders) {
        auto peer_folder = folder->get_folder_infos().by_device(*peer);
        if (!peer_folder) {
            continue;
        }
        auto my_folder = folder->get_folder_infos().by_device(*cluster.get_device());
        if (!my_folder) {
            continue;
        }
        prepare_folder(std::move(peer_folder));
    }
}

auto file_iterator_t::find_folder(folder_t *folder) noexcept -> folder_iterator_t & {
    auto predicate = [&](auto &it) -> bool { return it.peer_folder->get_folder() == folder; };
    auto it = std::find_if(folders_list.begin(), folders_list.end(), predicate);
    return *it;
}

auto file_iterator_t::prepare_folder(folder_info_ptr_t peer_folder) noexcept -> folder_iterator_t & {
    auto &files = peer_folder->get_file_infos();
    folders_list.emplace_back(folder_iterator_t{peer_folder, files.begin(), files.begin(), {}});
    return folders_list.back();
}

file_info_t *file_iterator_t::next_need_cloning() noexcept {
    auto folders_count = folders_list.size();
    auto folder_scans = size_t{0};

    while (folder_scans < folders_count) {
        auto &fi = folders_list[folder_index];
        auto &it = fi.it_clone;
        auto files_scan = size_t{0};
        auto &files_map = fi.peer_folder->get_file_infos();
        auto &folder_infos = fi.peer_folder->get_folder()->get_folder_infos();
        auto local_folder = folder_infos.by_device(*cluster.get_device());

        if (!local_folder->get_folder()->is_paused()) {
            while (files_scan < files_map.size()) {
                if (it == files_map.end()) {
                    it = files_map.begin();
                }
                auto &file = it->item;
                ++files_scan;
                ++it;

                if (!file->is_locally_locked() && !file->is_invalid() && file->is_global() && !file->is_invalid()) {
                    auto local = file->local_file();
                    bool need_clone = !local;
                    if (local) {
                        if (local->is_local()) {
                            using V = version_relation_t;
                            auto result = compare(file->get_version(), local->get_version());
                            need_clone = result == V::newer && file->is_locally_available();
                        }
                    } else {
                        need_clone = file->get_size() == 0;
                    }
                    if (need_clone) {
                        fi.guarded_clones.emplace(file->get_name(), new guard_t(file, *this));
                        return file.get();
                    }
                }

                if (files_scan == files_map.size()) {
                    break;
                }
            }
        }
        folder_index = (folder_index + 1) % folders_count;
        ++folder_scans;
    }
    return {};
}

file_info_t *file_iterator_t::next_need_sync() noexcept {
    auto folders_count = folders_list.size();
    auto folder_scans = size_t{0};

    while (folder_scans < folders_count) {
        auto &fi = folders_list[folder_index];
        auto local_folder = fi.peer_folder->get_folder()->get_folder_infos().by_device(*cluster.get_device());

        for (auto &[_, guard] : fi.guarded_syncs) {
            auto file = guard->file.get();
            if (file->local_file()) {
                return file;
            }
        }

        auto &it = fi.it_sync;
        auto files_scan = size_t{0};
        auto &files_map = fi.peer_folder->get_file_infos();

        // check other files
        if (!local_folder->get_folder()->is_paused()) {
            while (files_scan < files_map.size()) {
                if (it == files_map.end()) {
                    it = files_map.begin();
                }
                auto &file = it->item;
                ++files_scan;
                ++it;

                if (file->is_locally_locked()) {
                    continue;
                }

                if (file->is_unreachable()) {
                    continue;
                }

                if (file->is_invalid()) {
                    continue;
                }

                if (file->is_locally_available()) {
                    continue;
                }

                if (!file->get_size()) {
                    continue;
                }

                if (!file->is_global()) {
                    continue;
                }

                auto accept = true;

                if (auto local = file->local_file(); local) {
                    if (!local->is_local()) {
                        accept = false; // file is not has been scanned
                    } else {
                        using V = version_relation_t;
                        auto result = compare(file->get_version(), local->get_version());
                        accept = result == V::newer;
                    }
                }

                auto &seen_sequence = fi.committed_map[file];
                accept = accept && seen_sequence < file->get_sequence();

                if (accept) {
                    fi.guarded_syncs.emplace(file->get_name(), new guard_t(file, *this));
                    return file.get();
                }
            }
        }
        folder_index = (folder_index + 1) % folders_count;
        ++folder_scans;
    }
    return {};
}

void file_iterator_t::commit_clone(file_info_ptr_t file) noexcept {
    auto folder = file->get_folder_info()->get_folder();
    auto &fi = find_folder(folder);
    auto &guarded = fi.guarded_clones;
    auto it = guarded.find(file->get_name());
    // if needed for cloning files without iterator (i.e. in tests)
    if (it != guarded.end()) {
        fi.committed_map[file] = file->get_sequence();
        auto guard = std::move(*it);
        guarded.erase(it);
    }
}

void file_iterator_t::commit_sync(file_info_ptr_t file) noexcept {
    auto folder = file->get_folder_info()->get_folder();
    auto &fi = find_folder(folder);
    auto &guarded = fi.guarded_syncs;
    auto it = guarded.find(file->get_name());
    // if needed for cloning files without iterator (i.e. in tests)
    if (it != guarded.end()) {
        fi.committed_map[file] = file->get_sequence();
        guarded.erase(it);
    }
}

void file_iterator_t::on_clone(file_info_ptr_t file) noexcept {
    if (file->get_size() == 0) {
        commit_clone(file);
    }
}

void file_iterator_t::on_upsert(folder_info_ptr_t peer_folder) noexcept {
    auto folder = peer_folder->get_folder();
    auto &files = peer_folder->get_file_infos();
    for (auto &it : folders_list) {
        if (it.peer_folder->get_folder() == folder) {
            it.it_clone = it.it_sync = files.begin();
            return;
        }
    }
    prepare_folder(peer_folder);
}

void file_iterator_t::on_block_ack(const file_info_t &file, size_t block_index) {
    auto block = file.get_blocks().at(block_index);
    auto fi = file.get_folder_info();
    auto &committed_map = find_folder(fi->get_folder()).committed_map;
    for (auto &fb : block->get_file_blocks()) {
        auto f = fb.file();
        if (f->get_folder_info() == fi) {
            auto it = committed_map.find(f);
            if (it != committed_map.end()) {
                committed_map.erase(it);
            }
        }
    }
}

void file_iterator_t::on_remove(folder_info_ptr_t peer_folder) noexcept {
    for (auto it = folders_list.begin(); it != folders_list.end(); ++it) {
        if (it->peer_folder == peer_folder) {
            folders_list.erase(it);
            return;
        }
    }
}
