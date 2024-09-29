// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "file_iterator.h"
#include "../cluster.h"
#include <algorithm>

using namespace syncspirit::model;

file_iterator_t::file_iterator_t(cluster_t &cluster_, const device_ptr_t &peer_) noexcept
    : cluster{cluster_}, peer{peer_} {}

#if 0
bool file_iterator_t::append(file_info_t &file) noexcept {
    bool skip = file.is_locally_locked() || file.is_locked() || file.is_unreachable() || file.is_invalid();
    if (skip) {
        return false;
    }

    auto &folder_infos = file.get_folder_info()->get_folder()->get_folder_infos();
    auto local_folder = folder_infos.by_device(*cluster.get_device());

    auto local_file = local_folder->get_file_infos().by_name(file.get_name());
    if (!local_file) {
        if (!missing_done.count(&file)) {
            missing_done.emplace(&file);
            missing.push_back(&file);
            return true;
        }
    }
    if (!local_file->is_local()) {
        return false;
    }
    bool needs_download = local_file->need_download(file);
    if (!needs_download) {
        return false;
    }
    if (local_file->is_partly_available()) {
        if (!incomplete_done.count(&file)) {
            incomplete.push_back(&file);
            incomplete_done.emplace(&file);
            return true;
        }
    } else {
        if (!needed_done.count(&file)) {
            needed.push_back(&file);
            needed_done.emplace(&file);
            return true;
        }
    }
    return false;
}
#endif

bool file_iterator_t::accept(file_info_t &file) noexcept {
    if (file.is_unreachable() || file.is_invalid()) {
        return false;
    }

    auto &folder_infos = file.get_folder_info()->get_folder()->get_folder_infos();
    auto local_folder = folder_infos.by_device(*cluster.get_device());

    auto local_file = local_folder->get_file_infos().by_name(file.get_name());
    if (!local_file) {
        return true;
    }
    if (!local_file->is_local()) {
        return false;
    }
    return true;
}

file_info_ptr_t file_iterator_t::next() noexcept {
    auto file = file_info_ptr_t{};
    while (!folder_queue.empty()) {
        file = folder_queue.front();
        folder_queue.pop_front();
        if (file->is_locked() || file->is_locally_locked()) {
            locked_queue.emplace_back(file);
        } else if (accept(*file)) {
            return file->actualize();
        }
    }
    while (!locked_queue.empty()) {
        file = locked_queue.front();
        locked_queue.pop_front();
        if (file->is_locked() || file->is_locally_locked()) {
            continue;
        } else if (accept(*file)) {
            return file->actualize();
        }
    }

    auto &folders = cluster.get_folders();
    for (auto &[folder, _] : folders) {
        auto peer_folder = folder->get_folder_infos().by_device(*peer);
        if (!peer_folder || !peer_folder->is_actual()) {
            continue;
        }
        auto my_folder = folder->get_folder_infos().by_device(*cluster.get_device());
        if (!my_folder) {
            continue;
        }
        auto filter = visit_info_t{0, 0};
        auto it = visited.find(folder.get());
        if (it != visited.end()) {
            filter = it->second;
        }
        if (filter.index == peer_folder->get_index() && filter.visited_sequence == peer_folder->get_max_sequence()) {
            continue;
        }

        auto accept_all = filter.index != peer_folder->get_index();
        auto new_sequence = std::int64_t{};
        auto &files = peer_folder->get_file_infos();
        for (auto &[file, _] : files) {
            auto sequence = file->get_sequence();
            if (accept_all || sequence > filter.visited_sequence) {
                new_sequence = std::max(sequence, new_sequence);
                folder_queue.emplace_back(file);
            }
        }
        if (!folder_queue.empty()) {
            visited[folder.get()] = {peer_folder->get_index(), new_sequence};
            return next();
        }
    }
    return {};
}
