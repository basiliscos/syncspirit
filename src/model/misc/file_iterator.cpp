// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "file_iterator.h"
#include "../cluster.h"
#include <algorithm>

using namespace syncspirit::model;

file_iterator_t::file_iterator_t(cluster_t &cluster_, const device_ptr_t &peer_) noexcept
    : cluster{cluster_}, peer{peer_} {}

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

void file_iterator_t::requeue_content(queue_t queue) noexcept {
    std::move(queue.begin(), queue.end(), std::back_insert_iterator(content_queue));
}

file_info_ptr_t file_iterator_t::next() noexcept {
    auto file = file_info_ptr_t{};
    if (!content_queue.empty()) {
        file = content_queue.front();
        if (file->local_file()) {
            content_queue.pop_front();
            return file->actualize();
        }
    }
    for (auto it = locked_queue.begin(); it != locked_queue.end();) {
        auto f = *it;
        if (!f->is_locked() && !f->is_locally_locked()) {
            it = locked_queue.erase(it);
            if (accept(*f)) {
                return f->actualize();
            }
        } else {
            ++it;
        }
    }
    while (!folder_queue.empty()) {
        file = folder_queue.front();
        folder_queue.pop_front();
        if (file->is_locked() || file->is_locally_locked()) {
            locked_queue.emplace_back(file);
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
