// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <string>
#include <boost/filesystem.hpp>
#include "syncspirit-export.h"
#include "structs.pb.h"

namespace syncspirit::model {

namespace bfs = boost::filesystem;

struct SYNCSPIRIT_API folder_data_t {
    enum class foldet_type_t { send = 0, receive, send_and_receive };
    enum class pull_order_t { random = 0, alphabetic, largest, oldest, newest };

    const std::string &get_label() noexcept { return label; }
    std::string_view get_id() const noexcept { return id; }

  protected:
    inline const bfs::path &get_path() noexcept { return path; }
    void assign_fields(const db::Folder &item) noexcept;
    void serialize(db::Folder &dest) const noexcept;

    std::string id;
    std::string label;
    bfs::path path;
    foldet_type_t folder_type;
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
