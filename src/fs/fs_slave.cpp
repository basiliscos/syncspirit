// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "fs_slave.h"

using namespace syncspirit::fs;
using namespace syncspirit::fs::task;

bool fs_slave_t::exec(execution_context_t &context) noexcept {
    assert(!tasks_in.empty());
    auto any_update = false;
    while (!tasks_in.empty()) {
        auto &t = tasks_in.front();
        auto updated = std::visit([&](auto &task) { return task.process(*this, context); }, t);
        any_update = any_update | updated;
        tasks_out.emplace_back(std::move(t));
        tasks_in.pop_front();
    }
    return any_update;
}

void fs_slave_t::push(task_t task) noexcept { tasks_in.emplace_back(std::move(task)); }
