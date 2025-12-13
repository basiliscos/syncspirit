// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "presentation/presence.h"
#include "model/folder_info.h"
#include "fs/task/scan_dir.h"
#include <filesystem>
#include <boost/system/error_code.hpp>

namespace syncspirit::net::local_keeper {

namespace bfs = std::filesystem;
namespace sys = boost::system;

struct child_info_t {
    using blocks_t = std::vector<proto::BlockInfo>;

    child_info_t(fs::task::scan_dir_t::child_info_t backend, presentation::presence_ptr_t self_,
                 presentation::presence_ptr_t parent_);

    proto::FileInfo serialize(const model::folder_info_t &local_folder, blocks_t blocks, bool ignore_permissions);

    bfs::path path;
    bfs::path link_target;
    std::int64_t last_write_time;
    std::uintmax_t size;
    proto::FileInfoType type;
    std::uint32_t perms;
    sys::error_code ec;
    presentation::presence_ptr_t self;
    presentation::presence_ptr_t parent;
};

} // namespace syncspirit::net::local_keeper
