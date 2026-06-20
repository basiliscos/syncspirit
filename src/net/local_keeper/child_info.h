// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "presentation/presence.h"
#include "model/folder_info.h"
#include "fs/task/scan_dir.h"
#include "utils/path.h"
#include <boost/system/error_code.hpp>
#include <cstdint>

namespace syncspirit::net::local_keeper {

namespace sys = boost::system;

struct child_info_t : fs::task::scan_dir_t::child_info_t {
    using parent_t = fs::task::scan_dir_t::child_info_t;
    using blocks_t = std::vector<proto::BlockInfo>;
    using generation_t = std::uint_fast32_t;

    child_info_t(parent_t backend, presentation::presence_ptr_t self_, presentation::presence_ptr_t parent_,
                 generation_t generation_) noexcept;
    child_info_t(proto::FileInfo file_info, utils::path_t path, presentation::presence_ptr_t self_,
                 presentation::presence_ptr_t parent_, generation_t generation_) noexcept;
    child_info_t(child_info_t &&) noexcept = default;
    virtual ~child_info_t() = default;

    child_info_t clone() const noexcept;

    proto::FileInfo serialize(const model::folder_info_t &local_folder, blocks_t blocks, bool ignore_permissions);
    const model::file_info_t *fetch_model(const model::folder_info_t &local_folder) const;

    sys::error_code ec;
    presentation::presence_ptr_t self;
    presentation::presence_ptr_t parent;
    generation_t generation;
};

} // namespace syncspirit::net::local_keeper
