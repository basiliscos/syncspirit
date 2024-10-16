// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "file_iterator.h"
#include "../cluster.h"
#include "model/diff/modify/lock_file.h"
#include "model/diff/local/synchronization_start.h"
#include "model/diff/local/synchronization_finish.h"
#include "model/misc/version_utils.h"

using namespace syncspirit::model;

file_iterator_t::guard_t::guard_t(model::file_info_ptr_t file_, file_iterator_t &owner_) noexcept
    : file{file_}, owner{owner_} {
    assert(!file->is_locally_locked());
    file->locally_lock();

    is_locked = file->get_size() > 0;
    if (is_locked) {
        owner.sink->push(new model::diff::modify::lock_file_t(*file, true));
    }

    auto folder = file->get_folder_info()->get_folder();
    auto &it = owner.find_folder(folder);
    auto &guarded = it.guarded_files;
    if (guarded.size() == 0) {
        owner.sink->push(new model::diff::local::synchronization_start_t(folder->get_id()));
    }
}

file_iterator_t::guard_t::~guard_t() {
    file->locally_unlock();

    auto folder = file->get_folder_info()->get_folder();
    auto &it = owner.find_folder(folder);
    auto &guarded = it.guarded_files;
    if (guarded.size() == 1) {
        owner.sink->push(new model::diff::local::synchronization_finish_t(folder->get_id()));
    }

    if (is_locked) {
        owner.sink->push(new model::diff::modify::lock_file_t(*file, false));
    }
}

file_iterator_t::file_iterator_t(cluster_t &cluster_, const device_ptr_t &peer_) noexcept
    : cluster{cluster_}, peer{peer_}, folder_index{0}, sink{nullptr} {
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

void file_iterator_t::activate(diff_sink_t &sink_) noexcept {
    assert(!sink);
    sink = &sink_;
}

void file_iterator_t::deactivate() noexcept {
    assert(sink);
    sink = nullptr;
}

auto file_iterator_t::find_folder(folder_t *folder) noexcept -> folder_iterator_t & {
    for (auto &it : folders_list) {
        if (it.peer_folder->get_folder() == folder) {
            return it;
        }
    }
    assert(0 && "should not happen");
}

auto file_iterator_t::prepare_folder(folder_info_ptr_t peer_folder) noexcept -> folder_iterator_t & {
    auto &files = peer_folder->get_file_infos();
    folders_list.emplace_back(folder_iterator_t{peer_folder, files.begin(), files.begin(), {}});
    return folders_list.back();
}

file_info_t *file_iterator_t::next_need_cloning() noexcept {
    assert(sink);
    auto folders_count = folders_list.size();
    auto folder_scans = size_t{0};

    while (folder_scans < folders_count) {
        auto &fi = folders_list[folder_index];
        auto &it = fi.it_clone;
        auto files_scan = size_t{0};
        auto &files_map = fi.peer_folder->get_file_infos();
        auto local_folder = fi.peer_folder->get_folder()->get_folder_infos().by_device(*cluster.get_device());

        while (files_scan < files_map.size()) {
            if (it == files_map.end()) {
                it = files_map.begin();
            }
            auto &file = it->item;
            ++files_scan;
            ++it;

            if (!file->is_locally_locked() && !file->is_invalid()) {
                auto local_file = file->local_file();
                bool needed = false;
                if (!local_file) {
                    needed = true;
                } else if (local_file->is_local()) {
                    auto &v_peer = file->get_version();
                    auto &v_my = local_file->get_version();
                    needed = compare(v_my, v_peer) == version_relation_t::older;
                }

                if (needed) {
                    fi.guarded_files.emplace(file->get_name(), new guard_t(file, *this));
                    return file.get();
                }
            }

            if (files_scan == files_map.size()) {
                break;
            }
        }
        folder_index = (folder_index + 1) % folders_count;
        ++folder_scans;
    }
    return {};
}

file_info_t *file_iterator_t::next_need_sync() noexcept {
    assert(sink);
    auto folders_count = folders_list.size();
    auto folder_scans = size_t{0};

    while (folder_scans < folders_count) {
        auto &fi = folders_list[folder_index];
        auto local_folder = fi.peer_folder->get_folder()->get_folder_infos().by_device(*cluster.get_device());

        // check for already locked files (aka just cloned files)
        for (auto &[_, guard] : fi.guarded_files) {
            auto file = guard->file.get();
            if (file->local_file()) {
                return file;
            }
        }

        auto &it = fi.it_sync;
        auto files_scan = size_t{0};
        auto &files_map = fi.peer_folder->get_file_infos();

        // check other files
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

            // auto local_file = local_files_map.by_name(file->get_name());
            auto local_file = file->local_file();
            if (!local_file) {
                continue;
            }

            if (file->is_unreachable()) {
                continue;
            }

            auto &v_peer = file->get_version();
            auto &v_my = local_file->get_version();

            // clang-format off
            auto &seen_sequence = fi.committed_map[file];
            auto accept = seen_sequence < file->get_sequence()
                    && local_file->is_local()
                    && !local_file->is_locally_available()
                    && compare(v_my, v_peer) == version_relation_t::identity;
            // clang-format on
            if (accept) {
                fi.guarded_files.emplace(file->get_name(), new guard_t(file, *this));
                return file.get();
            }
        }
        folder_index = (folder_index + 1) % folders_count;
        ++folder_scans;
    }
    return {};
}

void file_iterator_t::commit(file_info_ptr_t file) noexcept {
    auto folder = file->get_folder_info()->get_folder();
    auto &fi = find_folder(folder);
    auto &guarded = fi.guarded_files;
    auto it = guarded.find(file->get_name());
    // if needed for cloning files without iterator (i.e. in tests)
    if (it != guarded.end()) {
        fi.committed_map[file] = file->get_sequence();
        guarded.erase(it);
    }
}

void file_iterator_t::on_clone(file_info_ptr_t file) noexcept {
    if (file->get_size() == 0) {
        commit(file);
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

#if 0
file_iterator_t::postponed_files_t::postponed_files_t(file_iterator_t &iterator_) : iterator{iterator_} {}

file_iterator_t::postponed_files_t::~postponed_files_t() {
    if (!postponed.empty()) {
        iterator.requeue_unchecked(std::move(postponed));
    }
}

file_iterator_t::file_iterator_t(cluster_t &cluster_, const device_ptr_t &peer_) noexcept
    : cluster{cluster_}, peer{peer_}, folder_index{0} {
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
        folders_list.emplace_back(prepare_folder(std::move(peer_folder)));
    }
}

bool file_iterator_t::accept(file_info_t &file, int folder_index, bool check_version) noexcept {
    if (file.is_unreachable() || file.is_invalid()) {
        return false;
    }

    auto peer_folder = file.get_folder_info();
    auto &folder_infos = peer_folder->get_folder()->get_folder_infos();
    auto local_folder = folder_infos.by_device(*cluster.get_device());

    auto local_file = local_folder->get_file_infos().by_name(file.get_name());
    if (!local_file) {
        return true;
    }
    if (!local_file->is_local()) {
        return false;
    }

    auto fi = (folder_iterator_t *){nullptr};
    if (folder_index >= 0)
        fi = &folders_list[folder_index];
    else {
        for (auto &f : folders_list) {
            if (f.peer_folder == peer_folder) {
                fi = &f;
                break;
            }
        }
    }
    assert(fi && "should not happen");

    if (check_version) {
        auto &v = file.get_version();
        auto version = v.counters(v.counters_size() - 1).value();
        auto &visited_version = fi->visited_map[&file];
        if (visited_version == version) {
            return false;
        }
    }
    if (local_file->need_download(file)) {
        return true;
    }
    if (!local_file->is_locally_available()) {
        return true;
    }
    return false;
}

#endif
