// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "../device.h"
#include "../file_info.h"
#include "../folder_info.h"
#include "../folder.h"
#include "syncspirit-export.h"
#include <deque>
#include <unordered_set>
#include <unordered_map>

namespace syncspirit::model {

struct cluster_t;
struct blocks_iterator_t;

struct SYNCSPIRIT_API file_iterator_t : arc_base_t<file_iterator_t> {
    using files_list_t = std::deque<file_info_ptr_t>;
    using files_set_t = std::unordered_set<file_info_ptr_t>;

    file_iterator_t(cluster_t &cluster, const device_ptr_t &peer) noexcept;
    file_iterator_t(const file_iterator_t &) = delete;

    file_info_ptr_t next() noexcept;
    void requeue_unchecked(files_set_t set) noexcept;
    void requeue_unchecked(file_info_ptr_t file) noexcept;
    void append_folder(folder_info_ptr_t peer_folder) noexcept;
    void append_folder(folder_info_ptr_t peer_folder, files_list_t queue) noexcept;
    void on_upsert(folder_info_ptr_t peer_folder) noexcept;

  private:
    using visited_map_t = std::unordered_map<file_info_ptr_t, std::uint64_t>;
    struct folder_iterator_t {
        model::folder_info_ptr_t peer_folder;
        std::uint64_t index;
        files_list_t files_list;
        std::size_t file_index;
        visited_map_t visited_map;
    };
    using folder_iterators_t = std::vector<folder_iterator_t>;

    bool accept(file_info_t &file, int folder_index, bool check_version = true) noexcept;
    folder_iterator_t prepare_folder(folder_info_ptr_t peer_folder) noexcept;

    file_info_ptr_t next_uncheked() noexcept;
    file_info_ptr_t next_locked() noexcept;
    file_info_ptr_t next_from_folder() noexcept;

    cluster_t &cluster;
    device_ptr_t peer;
    files_list_t uncheked_list;
    files_list_t locked_list;
    folder_iterators_t folders_list;
    std::size_t folder_index;
};

using file_iterator_ptr_t = intrusive_ptr_t<file_iterator_t>;

} // namespace syncspirit::model
