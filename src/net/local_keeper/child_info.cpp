// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "child_info.h"
#include "fs/utils.h"
#include "model/folder.h"
#include "model/folder_info.h"
#include "proto/proto-helpers-bep.h"

#include <boost/nowide/convert.hpp>

using namespace syncspirit::net::local_keeper;
using boost::nowide::narrow;

child_info_t::child_info_t(fs::task::scan_dir_t::child_info_t backend, presentation::presence_ptr_t self_,
                           presentation::presence_ptr_t parent_)
    : self(std::move(self_)), parent(std::move(parent_)) {
    path = std::move(backend.path);
    link_target = std::move(backend.target);
    last_write_time = fs::to_unix(backend.last_write_time);
    size = backend.size;
    type = [&]() -> proto::FileInfoType {
        using FT = proto::FileInfoType;
        auto t = backend.status.type();
        if (t == bfs::file_type::directory)
            return FT::DIRECTORY;
        else if (t == bfs::file_type::symlink)
            return FT::SYMLINK;
        else
            return FT::FILE;
    }();
    auto &status = backend.status;
    perms = static_cast<std::uint32_t>(status.permissions());
    ec = backend.ec;
}

auto child_info_t::serialize(const model::folder_info_t &local_folder, blocks_t blocks, bool ignore_permissions)
    -> proto::FileInfo {
    auto data = proto::FileInfo();
    auto name = fs::relativize(path, local_folder.get_folder()->get_path());
    proto::set_name(data, narrow(name.generic_wstring()));
    proto::set_type(data, type);
    proto::set_modified_s(data, last_write_time);
    if (size) {
        auto block_size = proto::get_size(blocks.front());
        proto::set_block_size(data, block_size);
        proto::set_size(data, size);
        proto::set_blocks(data, std::move(blocks));
    }
    if (ignore_permissions == false) {
        proto::set_permissions(data, perms);
    } else {
        proto::set_permissions(data, 0666);
        proto::set_no_permissions(data, true);
    }
    if (type == proto::FileInfoType::SYMLINK) {
        proto::set_symlink_target(data, narrow(link_target.generic_wstring()));
        proto::set_no_permissions(data, true);
    }
    return data;
}
