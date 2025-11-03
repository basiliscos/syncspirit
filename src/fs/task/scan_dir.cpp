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

scan_dir_t::scan_dir_t(bfs::path path_, presentation::presence_ptr_t presence_) noexcept
    : path{std::move(path_)}, presence{std::move(presence_)} {}

void scan_dir_t::process(fs_slave_t &slave, hasher::hasher_plugin_t *) noexcept {
    using FT = bfs::file_type;
    auto it = bfs::directory_iterator(path, ec);
    if (ec) {
        return;
    }
    for (; it != bfs::directory_iterator(); ++it) {
        auto &child = *it;
        auto child_info = task::scan_dir_t::child_info_t{};
        child_info.path = child;
        auto status = bfs::symlink_status(child_info.path, ec);
        if (ec) {
            child_info.ec = ec;
            child_infos.emplace_back(std::move(child_info));
            continue;
        } else {
            auto file_type = status.type();
            auto is_regular = file_type == FT::regular;
            auto is_dir = file_type == FT::directory;
            auto is_link = file_type == FT::symlink;
            if (!is_regular && !is_dir && !is_link) {
                continue;
            }
            auto &last = child_infos.emplace_back(std::move(child_info));
            last.status = std::move(status);

            if (is_regular || is_dir) {
                last.last_write_time = bfs::last_write_time(last.path, ec);
                if (ec) {
                    last.ec = ec;
                    continue;
                }
            }
            if (is_regular) {
                last.size = bfs::file_size(last.path, ec);
                if (ec) {
                    last.ec = ec;
                    continue;
                }
            } else if (is_link) {
                last.target = bfs::read_symlink(last.path, ec);
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
