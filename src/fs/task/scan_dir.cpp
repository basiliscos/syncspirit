// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "scan_dir.h"
#include "fs/fs_slave.h"
#include "utils/path_view.hpp"
#include "utils/path_utils.h"
#include <algorithm>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#else
#include <sys/types.h>
#include <dirent.h>
#endif

using namespace syncspirit;
using namespace syncspirit::fs;
using namespace syncspirit::fs::task;

// inverse files sorting as files will be inversed again (inderectly) by
// stack structure
struct comparator_t {
    bool operator()(const scan_dir_t::child_info_t &lhs, const scan_dir_t::child_info_t &rhs) const noexcept {
        auto l_dir = lhs.file_type == utils::file_type_t::DIRECTORY;
        auto r_dir = rhs.file_type == utils::file_type_t::DIRECTORY;
        if (l_dir xor r_dir) {
            return l_dir ? false : true;
        } else {
            return lhs.path.get_filename() > rhs.path.get_filename();
        }
    }
};

scan_dir_t::scan_dir_t(utils::path_t path_, presentation::presence_ptr_t presence_, utils::path_t single_child_, bool notify_,
                       bool recurse_, bool requires_refinement_) noexcept
    : path{std::move(path_)}, presence{std::move(presence_)},
      ec(utils::make_error_code(utils::error_code_t::no_action)), single_child{std::move(single_child_)},
      notify{notify_ ? 1u : 0}, recurse{recurse_ ? 1u : 0}, requires_refinement{requires_refinement_ ? 1u : 0} {}

bool scan_dir_t::process(fs_slave_t &slave, execution_context_t &context) noexcept {
    ec = {};

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#error "TODO"
#else
    auto d = ::opendir(path.get_full_name().data());
    auto dir_view = path.get_view(context.allocator);
    if (!d) {
        ec = std::error_code{errno, std::generic_category()};
    } else {
        auto entry = readdir(d);
        while (entry) {
            auto name = std::string_view(entry->d_name);
            if (!(name == "." || name == "..")) {
                if (single_child.empty() || single_child.get_filename() == name) {
                    auto child_path = dir_view / utils::make_native_view(name, context.allocator);
                    auto child_stats = utils::get_stats(child_path, ec);
                    if (!ec && child_stats.supported) {
                        auto child_info = task::scan_dir_t::child_info_t{};
                        child_info.path = child_path.detach();
                        child_info.file_type = child_stats.file_type;
                        child_info.permissions = child_stats.permissions;
                        child_info.last_write_time = child_stats.modification;
                        child_info.size = child_stats.file_size;
                        if (child_stats.file_type == utils::file_type_t::SYMLINK) {
                            auto link = utils::read_symlink(child_path, ec);
                            if (!ec) {
                                child_info.target = utils::path_t::make_native(link);
                            }
                        }
                        child_infos.push_back(std::move(child_info));
                    }
                }
            }
            entry = readdir(d);
        }
        if (closedir(d) != 0) {
            ec = std::error_code{errno, std::generic_category()};
        }
    }
#endif

    auto b = child_infos.begin();
    auto e = child_infos.end();
    std::sort(b, e, comparator_t());

    if (notify && context.scan_dir_callback) {
        context.scan_dir_callback(*this);
    }

    return false;
}
