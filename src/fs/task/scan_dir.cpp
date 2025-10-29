// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "scan_dir.h"
#include "fs/fs_slave.h"
#include <algorithm>

using namespace syncspirit::fs;
using namespace syncspirit::fs::task;

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

scan_dir_t::scan_dir_t(bfs::path path_) noexcept : path{std::move(path_)} {}

void scan_dir_t::process(fs_slave_t &slave, hasher::hasher_plugin_t *) noexcept {
    auto it = bfs::directory_iterator(path, ec);
    if (ec) {
        return;
    }
    for (; it != bfs::directory_iterator(); ++it) {
        auto &child = *it;
        auto child_info = task::scan_dir_t::child_info_t{};
        child_info.path = child;
        child_info.status = bfs::symlink_status(child, ec);
        auto &last = child_infos.emplace_back(std::move(child_info));
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
    auto b = child_infos.begin();
    auto e = child_infos.end();
    std::sort(b, e, comparator_t());
}
