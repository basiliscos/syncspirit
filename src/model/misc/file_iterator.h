// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "resolver.h"
#include "model/device.h"
#include "model/file_info.h"
#include "model/folder_info.h"
#include "model/folder.h"
#include "syncspirit-export.h"
#include <vector>
#include <set>
#include <memory>
#include <utility>

namespace syncspirit::model {

using compare_fn_t = bool(const file_info_t *l, const file_info_t *r);

struct SYNCSPIRIT_API file_iterator_t : arc_base_t<file_iterator_t> {
    using files_list_t = std::vector<file_info_ptr_t>;
    using result_t = std::pair<file_info_t *, advance_action_t>;

    file_iterator_t(cluster_t &cluster, const device_ptr_t &peer) noexcept;
    file_iterator_t(const file_iterator_t &) = delete;

    result_t next() noexcept;

    void on_upsert(folder_t &folder) noexcept;
    void on_upsert(folder_info_ptr_t peer_folder) noexcept;
    void on_remove(folder_info_ptr_t peer_folder) noexcept;
    void recheck(file_info_t &file) noexcept;

  private:
    struct file_comparator_t {
        bool operator()(const file_info_t *l, const file_info_t *r) const;
        db::PullOrder pull_order;
    };
    using file_comparator_ptr_t = std::unique_ptr<file_comparator_t>;
    using queue_t = std::set<file_info_t *, file_comparator_t>;
    using queue_ptr_t = std::unique_ptr<queue_t>;

    struct folder_iterator_t {
        using it_t = typename queue_t::iterator;
        model::folder_info_ptr_t peer_folder;
        queue_ptr_t files_queue;
        std::int64_t seen_sequence;
        it_t it;
        bool can_receive;
    };
    using folder_iterators_t = std::vector<folder_iterator_t>;

    folder_iterator_t &prepare_folder(folder_info_ptr_t peer_folder) noexcept;
    folder_iterator_t &find_folder(folder_t *folder) noexcept;

    cluster_t &cluster;
    device_t *peer;
    std::size_t folder_index;
    folder_iterators_t folders_list;
};

using file_iterator_ptr_t = intrusive_ptr_t<file_iterator_t>;

} // namespace syncspirit::model
