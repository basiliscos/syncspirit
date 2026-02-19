// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "scan_dir.h"
#include "fs/fs_slave.h"
#include <algorithm>

using namespace syncspirit::fs;
using namespace syncspirit::fs::task;

// inverse files sorting as files will be inversed again (inderectly) by
// stack structure
struct comparator_t {
    bool operator()(const scan_dir_t::child_info_t &lhs, const scan_dir_t::child_info_t &rhs) const noexcept {
        auto l_dir = lhs.status.type() == bfs::file_type::directory;
        auto r_dir = rhs.status.type() == bfs::file_type::directory;
        if (l_dir xor r_dir) {
            return l_dir ? false : true;
        } else {
            return lhs.path.filename() > rhs.path.filename();
        }
    }
};

scan_dir_t::scan_dir_t(bfs::path path_, presentation::presence_ptr_t presence_, bfs::path single_child_,
                       bool notify_) noexcept
    : path{std::move(path_)}, presence{std::move(presence_)},
      ec(utils::make_error_code(utils::error_code_t::no_action)), single_child{std::move(single_child_)},
      notify{notify_} {}

bool scan_dir_t::process(fs_slave_t &slave, execution_context_t &context) noexcept {
    ec = {};
    using FT = bfs::file_type;
    auto it = bfs::directory_iterator(path, ec);
    if (ec) {
        return false;
    }
    for (; it != bfs::directory_iterator(); ++it) {
        auto &child = *it;
        if (!single_child.empty()) {
            if (single_child.filename() != child.path().filename()) {
                continue;
            }
        }
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

    if (notify && context.scan_dir_callback) {
        context.scan_dir_callback(*this);
    }

    return false;
}
