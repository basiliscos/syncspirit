// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "../device.h"
#include "../file_info.h"
#include "../folder_info.h"
#include "../folder.h"
#include "syncspirit-export.h"
#include <deque>
#include <unordered_map>

namespace syncspirit::model {

struct cluster_t;
struct blocks_iterator_t;

struct SYNCSPIRIT_API file_iterator_t : arc_base_t<file_iterator_t> {
    using queue_t = std::deque<file_info_ptr_t>;

    file_iterator_t(cluster_t &cluster, const device_ptr_t &peer) noexcept;
    file_iterator_t(const file_iterator_t &) = delete;

    file_info_ptr_t next() noexcept;
    void requeue_content(queue_t queue) noexcept;

  private:
    struct visit_info_t {
        std::uint64_t index;
        std::int64_t visited_sequence;
    };

    using visited_folders_t = std::unordered_map<folder_t *, visit_info_t>;

    void prepare() noexcept;
    bool accept(file_info_t &file) noexcept;

    cluster_t &cluster;
    device_ptr_t peer;
    queue_t content_queue;
    queue_t folder_queue;
    queue_t locked_queue;
    visited_folders_t visited;
};

using file_iterator_ptr_t = intrusive_ptr_t<file_iterator_t>;

} // namespace syncspirit::model
