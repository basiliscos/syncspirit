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

namespace syncspirit::model {

struct cluster_t;
struct blocks_iterator_t;

struct SYNCSPIRIT_API file_iterator_t : arc_base_t<file_iterator_t> {
    file_iterator_t(cluster_t &cluster, const device_ptr_t &peer) noexcept;
    file_iterator_t(const file_iterator_t &) = delete;

    file_info_ptr_t next() noexcept;

  private:
    using queue_t = std::deque<file_info_ptr_t>;
    using files_set_t = std::unordered_set<file_info_ptr_t>;
    using folder_set_t = std::unordered_set<folder_ptr_t>;

    void prepare() noexcept;
    bool append(file_info_t &file) noexcept;

    cluster_t &cluster;
    device_ptr_t peer;
    queue_t missing;
    queue_t incomplete;
    queue_t needed;
    files_set_t missing_done;
    files_set_t incomplete_done;
    files_set_t needed_done;
    folder_set_t visited_folders;
};

using file_iterator_ptr_t = intrusive_ptr_t<file_iterator_t>;

} // namespace syncspirit::model
