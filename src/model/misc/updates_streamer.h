// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2023 Ivan Baidakou

#pragma once

#include <unordered_set>
#include "arc.hpp"
#include "model/cluster.h"
#include "model/file_info.h"
#include "model/device.h"
#include "model/folder_info.h"
#include "syncspirit-export.h"

namespace syncspirit::model {

struct SYNCSPIRIT_API updates_streamer_t {
    updates_streamer_t(cluster_t &, device_t &) noexcept;

    operator bool() const noexcept;
    file_info_ptr_t next() noexcept;

    void on_update(const folder_info_t &) noexcept;
    void on_update(const file_info_t &) noexcept;

  private:
    using folders_queue_t = std::unordered_set<folder_info_ptr_t>;

    void prepare() noexcept;

    device_ptr_t peer;
    folders_queue_t folders_queue;
};

} // namespace syncspirit::model
