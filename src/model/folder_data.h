// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include <string>
#include <filesystem>
#include "syncspirit-export.h"
#include "proto/proto-fwd.hpp"

namespace syncspirit::model {

namespace bfs = std::filesystem;

struct SYNCSPIRIT_API folder_data_t {
    using folder_type_t = syncspirit::db::FolderType;
    using pull_order_t = syncspirit::db::PullOrder;

    inline const std::string &get_label() const noexcept { return label; }
    inline std::string_view get_id() const noexcept { return id; }
    inline void set_id(std::string_view value) noexcept { id = value; }
    inline bool is_read_only() const noexcept { return read_only; }
    inline bool is_deletion_ignored() const noexcept { return ignore_delete; }
    inline bool are_permissions_ignored() const noexcept { return ignore_permissions; }
    inline bool are_temp_indixes_disabled() const noexcept { return disable_temp_indixes; }
    inline bool is_paused() const noexcept { return paused; }
    inline bool is_scheduled() const noexcept { return scheduled; }
    inline folder_type_t get_folder_type() const noexcept { return folder_type; }
    inline pull_order_t get_pull_order() const noexcept { return pull_order; }
    inline const bfs::path &get_path() const noexcept { return path; }
    inline void set_path(const bfs::path &value) noexcept { path = value; }
    inline std::uint32_t get_rescan_interval() const noexcept { return rescan_interval; };
    inline void set_rescan_interval(std::uint32_t value) noexcept { rescan_interval = value; };

    void serialize(syncspirit::db::Folder &dest) const noexcept;

    template <typename T> auto &access() noexcept;
    template <typename T> auto &access() const noexcept;

  protected:
    void assign_fields(const db::Folder &item) noexcept;

    std::string id;
    std::string label;
    bfs::path path;
    folder_type_t folder_type;
    std::uint32_t rescan_interval;
    pull_order_t pull_order;
    bool scheduled;
    bool read_only;
    bool ignore_permissions;
    bool ignore_delete;
    bool disable_temp_indixes;
    bool paused;
};

} // namespace syncspirit::model
