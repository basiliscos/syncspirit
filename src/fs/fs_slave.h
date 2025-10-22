// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "messages.h"
#include "task/scan_dir.h"
#include "syncspirit-export.h"
#include <variant>
#include <list>

namespace syncspirit::fs {

using task_t = std::variant<task::scan_dir_t>;

struct SYNCSPIRIT_API fs_slave_t : payload::foreign_executor_t {
    using tasks_t = std::list<task_t>;

    void exec() noexcept override;
    void push(task_t task) noexcept;

    void process(task::scan_dir_t &) noexcept;

    tasks_t tasks_in;
    tasks_t tasks_out;
};

} // namespace syncspirit::fs
