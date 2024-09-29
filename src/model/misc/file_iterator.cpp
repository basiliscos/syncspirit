// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "file_iterator.h"
#include "../cluster.h"

using namespace syncspirit::model;

file_iterator_t::file_iterator_t(cluster_t &cluster_, const device_ptr_t &peer_) noexcept
    : cluster{cluster_}, peer{peer_} {}

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

file_info_ptr_t file_iterator_t::next() noexcept {
    file_info_ptr_t file;
    if (!incomplete.empty()) {
        file = incomplete.front();
        incomplete.pop_front();
    } else if (!needed.empty()) {
        file = needed.front();
        needed.pop_front();
    } else if (!missing.empty()) {
        file = missing.front();
        missing.pop_front();
    }
    if (file) {
        return file->actualize();
    }

    auto &folders = cluster.get_folders();
    for (auto &it : folders) {
        auto &folder = it.item;
        if (visited_folders.count(folder)) {
            continue;
        }
        auto peer_folder = folder->get_folder_infos().by_device(*peer);
        if (!peer_folder || !peer_folder->is_actual()) {
            continue;
        }
        auto my_folder = folder->get_folder_infos().by_device(*cluster.get_device());
        if (!my_folder) {
            continue;
        }
        visited_folders.emplace(folder);
        bool something_added = false;
        for (auto &fit : peer_folder->get_file_infos()) {
            something_added = append(*fit.item) || something_added;
        }
        if (something_added) {
            return next();
        }
    }
    return {};
}
