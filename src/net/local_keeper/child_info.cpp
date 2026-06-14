// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "child_info.h"
#include "model/folder.h"
#include "model/folder_info.h"
#include "proto/proto-helpers-bep.h"

#include <cassert>

using namespace syncspirit::net::local_keeper;

child_info_t::child_info_t(parent_t backend, presentation::presence_ptr_t self_,
                           presentation::presence_ptr_t parent_, generation_t generation_) noexcept
    : parent_t(std::move(backend)), self(std::move(self_)), parent(std::move(parent_)), generation{generation_} {
    assert(!path.empty());
    ec = backend.ec;
}

child_info_t::child_info_t(proto::FileInfo info, utils::path_t path_, presentation::presence_ptr_t self_,
                           presentation::presence_ptr_t parent_, generation_t generation_) noexcept
    : self{std::move(self_)}, parent(std::move(parent_)), generation{generation_} {
    path = std::move(path_);
    assert(!path.empty());
    size = proto::get_size(info);
    target = utils::path_t::make_native(proto::get_symlink_target(info));
    last_write_time = proto::get_modified_s(info);
    size = proto::get_size(info);
    file_type = proto::get_type(info);
    permissions = proto::get_permissions(info);
}

child_info_t child_info_t::clone() const noexcept {
    auto new_parent = parent_t {
        path.clone(),
        target.clone(),
        file_type,
        permissions,
        last_write_time,
        size,
        ec
    };
    return child_info_t(std::move(new_parent), self, parent, generation);
}

auto child_info_t::serialize(const model::folder_info_t &local_folder, blocks_t blocks, bool ignore_permissions)
    -> proto::FileInfo {
    auto data = proto::FileInfo();
    auto name = path.relativize(local_folder.get_folder()->get_path());
    proto::set_name(data, name);
    proto::set_type(data, file_type);
    proto::set_modified_s(data, last_write_time);
    if (size) {
        auto block_size = proto::get_size(blocks.front());
        proto::set_block_size(data, block_size);
        proto::set_size(data, size);
        proto::set_blocks(data, std::move(blocks));
    }
    if (ignore_permissions == false) {
        proto::set_permissions(data, permissions);
    } else {
        proto::set_permissions(data, 0666);
        proto::set_no_permissions(data, true);
    }
    if (file_type == proto::FileInfoType::SYMLINK) {
        proto::set_symlink_target(data, target.get_full_name());
        proto::set_no_permissions(data, true);
    }
    return data;
}

auto child_info_t::fetch_model(const model::folder_info_t &local_folder) const -> const model::file_info_t * {
    auto &folder_path = local_folder.get_folder()->get_path();
    auto name = path.relativize(local_folder.get_folder()->get_path());
    return local_folder.get_file_infos().by_name(name).get();
}
