// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#pragma once

#include "task.h"
#include "presentation/presence.h"
#include <memory>

namespace syncspirit::fs::task {

struct SYNCSPIRIT_API scan_dir_t {
    struct child_info_t {
        bfs::path path;
        bfs::path target;
        bfs::file_status status;
        bfs::file_time_type last_write_time = {};
        std::uintmax_t size;
        sys::error_code ec;
    };
    struct custom_payload_t {
        virtual ~custom_payload_t() = default;
    };
    using child_infos_t = std::vector<child_info_t>;
    using custom_payload_ptr_t = std::unique_ptr<custom_payload_t>;

    scan_dir_t(bfs::path path, presentation::presence_ptr_t presence, custom_payload_ptr_t payload) noexcept;
    bool process(fs_slave_t &fs_slave, execution_context_t &context) noexcept;

    bfs::path path;
    presentation::presence_ptr_t presence;
    sys::error_code ec;
    child_infos_t child_infos;
    custom_payload_ptr_t payload;
};

} // namespace syncspirit::fs::task
