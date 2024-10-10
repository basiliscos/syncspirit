// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "file_iterator.h"
#include "../cluster.h"

using namespace syncspirit::model;

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

void file_iterator_t::requeue_unchecked(files_set_t set) noexcept {
    for (auto &file : set) {
        requeue_unchecked(std::move(file));
    }
}

void file_iterator_t::requeue_unchecked(file_info_ptr_t file) noexcept {
    if (accept(*file, -1, false)) {
        uncheked_list.emplace_back(std::move(file));
    }
}

file_info_ptr_t file_iterator_t::next_uncheked() noexcept {
    if (!uncheked_list.empty()) {
        auto file = uncheked_list.front();
        uncheked_list.pop_front();
        return file->actualize();
    }
    return {};
}

file_info_ptr_t file_iterator_t::next_locked() noexcept {
    for (auto it = locked_list.begin(); it != locked_list.end();) {
        auto f = *it;
        if (!f->is_locked() && !f->is_locally_locked()) {
            it = locked_list.erase(it);
            return f->actualize();
        } else {
            ++it;
        }
    }
    return {};
}

file_info_ptr_t file_iterator_t::next_from_folder() noexcept {
    auto folders_count = folders_list.size();
    auto scan_count = size_t{0};

    while (scan_count < folders_count) {
        auto &fi = folders_list[folder_index];
        if (fi.file_index >= fi.files_list.size()) {
            fi.file_index = 0;
            ++scan_count;
            folder_index = (folder_index + 1) % folders_count;
            continue;
        }
        while (fi.file_index < fi.files_list.size()) {
            auto file = fi.files_list[fi.file_index++];
            if (accept(*file, static_cast<int>(folder_index))) {
                if (file->is_locked() || file->is_locally_locked()) {
                    locked_list.emplace_back(std::move(file));
                } else {
                    return file->actualize();
                }
            } else if (accept(*file, static_cast<int>(folder_index))) {
            }
        }
    }
    return {};
}

auto file_iterator_t::prepare_postponed() noexcept -> postponed_files_t { return postponed_files_t(*this); }

file_info_t *file_iterator_t::current() noexcept { return current_file.get(); }

auto file_iterator_t::next() noexcept -> file_info_t * {
    assert(!current_file);
    while (true) {
        current_file = next_uncheked();
        if (!current_file)
            current_file = next_locked();
        if (!current_file)
            current_file = next_from_folder();
        break;
    }
    return current_file.get();
}

void file_iterator_t::done() noexcept {
    assert(current_file);
    current_file.reset();
}

void file_iterator_t::postpone(postponed_files_t &postponed) noexcept {
    assert(current_file);
    postponed.postponed.emplace(current_file);
}

auto file_iterator_t::prepare_folder(folder_info_ptr_t peer_folder) noexcept -> folder_iterator_t {
    auto fi = folder_iterator_t{};
    fi.peer_folder = peer_folder;
    fi.index = peer_folder->get_index();
    fi.file_index = 0;
    auto &files = peer_folder->get_file_infos();
    for (auto &[file, _] : files) {
        fi.files_list.push_back(file);
        fi.visited_map[file] = 0;
    }
    return fi;
}

void file_iterator_t::on_upsert(folder_info_ptr_t folder_info) noexcept {
    for (auto &fi : folders_list) {
        if (fi.peer_folder == folder_info) {
            if (fi.index != folder_info->get_index()) {
                fi = prepare_folder(folder_info);
            }
            return;
        }
    }
    folders_list.emplace_back(prepare_folder(std::move(folder_info)));
}

void file_iterator_t::append_folder(folder_info_ptr_t peer_folder, files_list_t queue) noexcept {
    for (auto &fi : folders_list) {
        if (fi.peer_folder == peer_folder) {
            for (auto &file : queue) {
                assert(file);
                if (fi.visited_map.count(file) == 0) {
                    fi.files_list.emplace_back(file);
                    fi.visited_map[file] = 0;
                }
            }
        }
    }
}
