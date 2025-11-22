// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "messages.h"
#include "task/remove_file.h"
#include "task/rename_file.h"
#include "task/scan_dir.h"
#include "task/segment_iterator.h"
#include "syncspirit-export.h"
#include <variant>
#include <list>

namespace syncspirit::fs {

namespace sys = boost::system;
using task_t = std::variant<task::scan_dir_t, task::segment_iterator_t, task::remove_file_t, task::rename_file_t>;

struct SYNCSPIRIT_API fs_slave_t : payload::foreign_executor_t {
    using tasks_t = std::list<task_t>;

    void exec(hasher::hasher_plugin_t *hasher) noexcept override;
    void push(task_t task) noexcept;

    void process(task::scan_dir_t &) noexcept;

    tasks_t tasks_in;
    tasks_t tasks_out;
    sys::error_code ec;
};

} // namespace syncspirit::fs
