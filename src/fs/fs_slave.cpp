// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "fs_slave.h"

#include <algorithm>

using namespace syncspirit::fs;
using namespace syncspirit::fs::task;

scan_dir_t::scan_dir_t(bfs::path path_) noexcept : path{std::move(path_)} {}

struct comparator_t {
    bool operator()(const scan_dir_t::child_info_t &lhs, const scan_dir_t::child_info_t &rhs) const noexcept {
        auto l_dir = lhs.status.type() == bfs::file_type::directory;
        auto r_dir = rhs.status.type() == bfs::file_type::directory;
        if (l_dir xor r_dir) {
            return l_dir ? true : false;
        } else {
            return lhs.path.filename() < rhs.path.filename();
        }
    }
};

void fs_slave_t::exec() noexcept {
    while (!tasks_in.empty()) {
        auto &t = tasks_in.front();
        std::visit([this](auto &arg) { process(arg); }, t);
        tasks_out.emplace_back(std::move(t));
        tasks_in.pop_front();
    }
}

void fs_slave_t::push(task_t task) noexcept { tasks_in.emplace_back(std::move(task)); }

void fs_slave_t::process(task::scan_dir_t &task) noexcept {
    auto ec = sys::error_code();
    auto it = bfs::directory_iterator(task.path, ec);
    if (ec) {
        task.ec = ec;
        return;
    }
    for (; it != bfs::directory_iterator(); ++it) {
        auto &child = *it;
        auto child_info = task::scan_dir_t::child_info_t{};
        child_info.path = child;
        child_info.status = bfs::symlink_status(child, ec);
        auto &last = task.child_infos.emplace_back(std::move(child_info));
        if (ec) {
            last.ec = ec;
            continue;
        } else {
            auto file_type = last.status.type();
            bool get_mtime = (file_type == bfs::file_type::regular || file_type == bfs::file_type::directory);
            if (get_mtime) {
                last.last_write_time = bfs::last_write_time(child, ec);
                if (ec) {
                    last.ec = ec;
                    continue;
                }
            }
            if (file_type == bfs::file_type::regular) {
                last.size = bfs::file_size(child, ec);
                if (ec) {
                    last.ec = ec;
                    continue;
                }
            } else if (file_type == bfs::file_type::symlink) {
                last.target = bfs::read_symlink(child, ec);
                if (ec) {
                    last.ec = ec;
                    continue;
                }
            }
        }
    }
    auto b = task.child_infos.begin();
    auto e = task.child_infos.end();
    std::sort(b, e, comparator_t());
}
