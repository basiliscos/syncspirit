// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#pragma once

#include "resolver.h"
#include "model/device.h"
#include "model/file_info.h"
#include "model/folder_info.h"
#include "model/folder.h"
#include "syncspirit-export.h"
#include <vector>

namespace syncspirit::model {

struct folder_info_t;
using folder_info_ptr_t = intrusive_ptr_t<folder_info_t>;

struct SYNCSPIRIT_API file_iterator_t : arc_base_t<file_iterator_t> {
    using files_list_t = std::vector<file_info_ptr_t>;
    struct result_t {
        file_info_t *peer_file;
        folder_info_t *peer_folder;
        file_info_t *local_file;
        advance_action_t action;
    };

    file_iterator_t(cluster_t &cluster, const device_ptr_t &peer) noexcept;
    file_iterator_t(const file_iterator_t &) = delete;
    ~file_iterator_t();

    result_t next() noexcept;

    void on_upsert(folder_t &folder) noexcept;
    void on_upsert(folder_info_ptr_t peer_folder) noexcept;
    void on_remove(const folder_info_t* peer_folder) noexcept;
    void recheck(const folder_info_t &fi, file_info_t &file) noexcept;

  private:
    struct substate_t {
        folder_info_ptr_t fi;
        folder_info_iterator_ptr_t it;
        std::uint64_t seen_index;
        std::int64_t seen_sequence;
    };
    using substates_t = std::vector<substate_t>;
#if 0
    using file_comparator_ptr_t = std::unique_ptr<file_comparator_t>;
    using queue_t = std::set<file_info_t *, file_comparator_t>;
    using queue_ptr_t = std::unique_ptr<queue_t>;

    struct folder_iterator_t {
        using it_t = typename queue_t::iterator;
        model::folder_info_ptr_t peer_folder;
        queue_ptr_t files_queue;
        std::uint64_t seen_index;
        std::int64_t seen_sequence;
        it_t it;
        bool can_receive;
    };
    using folder_iterators_t = std::vector<folder_iterator_t>;

    folder_iterator_t &prepare_folder(folder_info_ptr_t peer_folder) noexcept;
    folder_iterator_t &find_folder(folder_t *folder) noexcept;
    void populate(folder_iterator_t &it) noexcept;

    std::size_t folder_index;
    folder_iterators_t folders_list;
#endif
    void check(model::folder_info_t* fi) noexcept;
    std::size_t folder_index;
    cluster_t &cluster;
    device_t *peer;
    substates_t substates;
};

using file_iterator_ptr_t = intrusive_ptr_t<file_iterator_t>;

} // namespace syncspirit::model
