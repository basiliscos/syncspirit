// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "fs_slave.h"

using namespace syncspirit::fs;
using namespace syncspirit::fs::task;

void fs_slave_t::exec() noexcept {
    while (!tasks_in.empty()) {
        auto &t = tasks_in.front();
        std::visit([this](auto &task) { task.process(*this); }, t);
        tasks_out.emplace_back(std::move(t));
        tasks_in.pop_front();
    }
}

void fs_slave_t::push(task_t task) noexcept { tasks_in.emplace_back(std::move(task)); }
