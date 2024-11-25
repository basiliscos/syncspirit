// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "model/device.h"
#include "model/file_info.h"
#include "model/folder_info.h"
#include "model/folder.h"
#include "syncspirit-export.h"
#include <vector>
#include <unordered_map>

namespace syncspirit::model {

struct SYNCSPIRIT_API file_iterator_t : arc_base_t<file_iterator_t> {
    using files_list_t = std::vector<file_info_ptr_t>;

    file_iterator_t(cluster_t &cluster, const device_ptr_t &peer) noexcept;
    file_iterator_t(const file_iterator_t &) = delete;

    file_info_t *next() noexcept;
    void commit_sync(file_info_ptr_t file) noexcept;

    void on_upsert(folder_info_ptr_t peer_folder) noexcept;
    void on_remove(folder_info_ptr_t peer_folder) noexcept;

  private:
    using guarded_files_t = std::unordered_map<std::string_view, file_info_t::guard_ptr_t>;

    struct folder_iterator_t {
        using it_t = typename model::file_infos_map_t::iterator_t;
        model::folder_info_ptr_t peer_folder;
        it_t it_clone;
        it_t it_sync;
        guarded_files_t guarded;
    };
    using folder_iterators_t = std::vector<folder_iterator_t>;

    void commit_clone(file_info_ptr_t file) noexcept;
    folder_iterator_t &prepare_folder(folder_info_ptr_t peer_folder) noexcept;
    folder_iterator_t &find_folder(folder_t *folder) noexcept;

    cluster_t &cluster;
    device_t *peer;
    std::size_t folder_index;
    folder_iterators_t folders_list;
};

using file_iterator_ptr_t = intrusive_ptr_t<file_iterator_t>;

} // namespace syncspirit::model
