// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "messages.h"
#include "syncspirit-export.h"
#include <variant>
#include <list>

namespace syncspirit::fs {

namespace task {

struct SYNCSPIRIT_API scan_dir_t {
    struct child_info_t {
        bfs::path path;
        bfs::path target;
        bfs::file_status status;
        bfs::file_time_type last_write_time;
        std::uintmax_t size;
        sys::error_code ec;
    };
    using child_infos_t = std::vector<child_info_t>;

    scan_dir_t(bfs::path path) noexcept;

    bfs::path path;
    sys::error_code ec;
    child_infos_t child_infos;
};

} // namespace task

using task_t = std::variant<task::scan_dir_t>;

struct SYNCSPIRIT_API fs_slave_t final : payload::foreign_executor_t {
    using tasks_t = std::list<task_t>;

    void exec() noexcept override;
    void push(task_t task) noexcept;

    void process(task::scan_dir_t &) noexcept;

    tasks_t tasks_in;
    tasks_t tasks_out;
};

} // namespace syncspirit::fs
