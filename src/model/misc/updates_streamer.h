// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2023-2026 Ivan Baidakou

#pragma once

#include "model/cluster.h"
#include "model/file_info.h"
#include "model/device.h"
#include "model/folder_info.h"
#include "model/misc/folder_info_iterator.h"
#include "syncspirit-export.h"
#include <deque>
#include <vector>
#include <tuple>

namespace syncspirit::model {

struct SYNCSPIRIT_API updates_streamer_t {
    using update_t = std::tuple<file_info_ptr_t, folder_info_t *, bool>;

    updates_streamer_t(cluster_t &, device_t &) noexcept;
    updates_streamer_t(const updates_streamer_t &) noexcept = delete;
    updates_streamer_t(updates_streamer_t &&) noexcept = delete;

    update_t next() noexcept;

    bool on_update(file_info_t &, const folder_info_t &) noexcept;
    void on_remote_refresh() noexcept;
    void on_upsert(const folder_info_t &) noexcept;

  private:
    using updated_files_t = std::deque<model::file_info_t*>;
    struct update_info_t {
        folder_info_ptr_t local_folder;
        updated_files_t updated_files;
        std::int64_t seen_sequence{0};
        bool fetched{false};
        void fetch() noexcept;
    };
    using update_infos_t = std::vector<update_info_t>;

    void refresh_remote() noexcept;

    cluster_t &cluster;
    device_ptr_t peer;
    update_infos_t update_infos;
    int current{-1};
};

} // namespace syncspirit::model
