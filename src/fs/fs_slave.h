// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "messages.h"
#include "task/tasks.h"
#include "syncspirit-export.h"

namespace syncspirit::fs {

namespace sys = boost::system;

struct SYNCSPIRIT_API fs_slave_t : payload::foreign_executor_t {
    bool exec(execution_context_t &context) noexcept override;
    void push(task_t task) noexcept;

    void process(task::scan_dir_t &) noexcept;

    tasks_t tasks_in;
    tasks_t tasks_out;
    sys::error_code ec;
};

} // namespace syncspirit::fs
