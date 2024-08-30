// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "file_iterator.h"
#include "../cluster.h"

using namespace syncspirit::model;

file_iterator_t::file_iterator_t(cluster_t &cluster_, const device_ptr_t &peer_) noexcept
    : cluster{cluster_}, peer{peer_} {
    reset();
    prepare();
}

void file_iterator_t::append(file_info_t &file) noexcept {
    bool skip = file.is_locally_locked() || file.is_locked() || file.is_unreachable() || file.is_invalid();
    if (skip) {
        return;
    }

    auto &folder_infos = file.get_folder_info()->get_folder()->get_folder_infos();
    auto local_folder = folder_infos.by_device(*cluster.get_device());

    auto local_file = local_folder->get_file_infos().by_name(file.get_name());
    if (!local_file) {
        if (!missing_done.count(&file)) {
            missing_done.emplace(&file);
            missing.push_back(&file);
        }
        return;
    }
    if (!local_file->is_local()) {
        return;
    }
    bool needs_download = local_file->need_download(file);
    if (!needs_download) {
        return;
    }
    if (local_file->is_partly_available()) {
        if (!incomplete_done.count(&file)) {
            incomplete.push_back(&file);
            incomplete_done.emplace(&file);
        }
    } else {
        if (!needed_done.count(&file)) {
            needed.push_back(&file);
            needed_done.emplace(&file);
        }
    }
}

void file_iterator_t::reset() noexcept {
    auto &folders = cluster.get_folders();
    for (auto &it : folders) {
        auto &folder = *it.item;
        auto peer_folder = folder.get_folder_infos().by_device(*peer);
        if (!peer_folder || !peer_folder->is_actual()) {
            continue;
        }
        auto my_folder = folder.get_folder_infos().by_device(*cluster.get_device());
        if (!my_folder) {
            continue;
        }
        for (auto &fit : peer_folder->get_file_infos()) {
            append(*fit.item);
        }
    }
}

void file_iterator_t::renew(file_info_t &f) noexcept {
    append(f);
    if (!file) {
        prepare();
    }
}

void file_iterator_t::prepare() noexcept {
    if (!incomplete.empty()) {
        file = incomplete.front();
        incomplete.pop_front();
        return;
    }
    if (!needed.empty()) {
        file = needed.front();
        needed.pop_front();
        return;
    }
    while (!missing.empty()) {
        file = missing.front();
        missing.pop_front();
        return;
    }
    file = nullptr;
}

file_iterator_t::operator bool() const noexcept { return (bool)file; }

file_info_ptr_t file_iterator_t::next() noexcept {
    auto r = file_info_ptr_t(std::move(file));
    prepare();
    if (r) {
        return r->actualize();
    }
    return r;
}
