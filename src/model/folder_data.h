// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include <string>
#include <boost/filesystem.hpp>
#include "syncspirit-export.h"
#include "structs.pb.h"

namespace syncspirit::model {

namespace bfs = boost::filesystem;

struct SYNCSPIRIT_API folder_data_t {
    enum class folder_type_t { send = 0, receive, send_and_receive };
    enum class pull_order_t { random = 0, alphabetic, smallest, largest, oldest, newest };

    inline const std::string &get_label() noexcept { return label; }
    inline std::string_view get_id() const noexcept { return id; }
    inline bool is_read_only() const noexcept { return read_only; }
    inline bool is_deletion_ignored() const noexcept { return ignore_delete; }
    inline bool are_permissions_ignored() const noexcept { return ignore_permissions; }
    inline bool are_temp_indixes_disabled() const noexcept { return disable_temp_indixes; }
    inline bool is_paused() const noexcept { return paused; }
    inline folder_type_t get_folder_type() const noexcept { return folder_type; }
    inline pull_order_t get_pull_order() const noexcept { return pull_order; }
    inline const bfs::path &get_path() noexcept { return path; }
    void serialize(db::Folder &dest) const noexcept;

  protected:
    void set_path(const bfs::path &value) noexcept { path = value; }
    void assign_fields(const db::Folder &item) noexcept;

    std::string id;
    std::string label;
    bfs::path path;
    folder_type_t folder_type;
    std::uint32_t rescan_interval;
    pull_order_t pull_order;
    bool watched;
    bool read_only;
    bool ignore_permissions;
    bool ignore_delete;
    bool disable_temp_indixes;
    bool paused;
};

} // namespace syncspirit::model
