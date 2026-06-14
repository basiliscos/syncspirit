// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#pragma once

#include "task.h"
#include <cstdint>
#include "presentation/presence.h"

namespace syncspirit::fs::task {

struct SYNCSPIRIT_API scan_dir_t {
    struct child_info_t {
        utils::path_t path;
        utils::path_t target;
        utils::file_type_t file_type;
        std::uint32_t permissions;
        std::int64_t last_write_time;
        std::int64_t size;
        std::error_code ec;
    };
    using child_infos_t = std::vector<child_info_t>;

    scan_dir_t(utils::path_t path, presentation::presence_ptr_t presence, utils::path_t single_child, bool notify, bool recurse,
               bool requires_refinement) noexcept;
    bool process(fs_slave_t &fs_slave, execution_context_t &context) noexcept;

    utils::path_t path;
    presentation::presence_ptr_t presence;
    std::error_code ec;
    child_infos_t child_infos;
    utils::path_t single_child;
    unsigned notify : 1;
    unsigned recurse : 1;
    unsigned requires_refinement : 1;
};

} // namespace syncspirit::fs::task
