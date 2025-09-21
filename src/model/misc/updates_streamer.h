// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2023-2024 Ivan Baidakou

#pragma once

#include "model/cluster.h"
#include "model/file_info.h"
#include "model/device.h"
#include "model/folder_info.h"
#include "model/remote_folder_info.h"
#include "syncspirit-export.h"
#include <optional>
#include <unordered_map>
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
    struct streaming_info_t {
        folder_info_ptr_t folder_info;
        file_infos_map_t unseen_files;
    };
    using streaming_option_t = std::optional<streaming_info_t>;
    using seen_info_t = std::unordered_map<folder_info_ptr_t, std::int64_t>;
    void refresh_remote() noexcept;

    cluster_t &cluster;
    device_ptr_t peer;
    device_ptr_t self;
    seen_info_t seen_info;
    streaming_option_t streaming;
};

} // namespace syncspirit::model
