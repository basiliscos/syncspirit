// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "../device.h"
#include "../file_info.h"
#include "../folder_info.h"
#include "../folder.h"
#include "syncspirit-export.h"
#include <deque>

namespace syncspirit::model {

struct cluster_t;
struct blocks_iterator_t;

struct SYNCSPIRIT_API file_iterator_t : arc_base_t<file_iterator_t> {
    file_iterator_t(cluster_t &cluster, const device_ptr_t &peer) noexcept;
    file_iterator_t(const file_iterator_t &) = delete;

    operator bool() const noexcept;

    file_info_ptr_t next() noexcept;
    void reset() noexcept;

  private:
    using queue_t = std::deque<file_info_ptr_t>;
    using set_t = std::unordered_set<file_info_ptr_t>;

    void prepare() noexcept;
    void append(file_info_t &file) noexcept;

    cluster_t &cluster;
    device_ptr_t peer;
    file_info_ptr_t file;
    queue_t missing;
    queue_t incomplete;
    queue_t needed;
    set_t missing_done;
    set_t incomplete_done;
    set_t needed_done;
};

using file_iterator_ptr_t = intrusive_ptr_t<file_iterator_t>;

} // namespace syncspirit::model
