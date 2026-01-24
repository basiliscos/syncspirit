// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "rename_file.h"
#include "fs/updates_mediator.h"
#include "fs/utils.h"
#include <boost/nowide/convert.hpp>

using namespace syncspirit::fs::task;
using boost::nowide::narrow;

rename_file_t::rename_file_t(bfs::path path_, bfs::path new_name_, std::int64_t modification_s_,
                             hasher::payload::extendended_context_prt_t context_) noexcept
    : path{std::move(path_)}, new_name{std::move(new_name_)}, modification_s{modification_s_},
      context{std::move(context_)} {}

bool rename_file_t::process(fs_slave_t &fs_slave, execution_context_t &context) noexcept {
    ec = {};
    auto new_path = path.parent_path() / new_name;
    bfs::rename(path, new_path, ec);
    if (!ec) {
        if (auto mediator = context.mediator; mediator) {
            auto path_str = narrow(path.generic_wstring());
            auto new_path_str = narrow(new_path.generic_wstring());
            mediator->push(path_str, context.get_deadline());
            mediator->push(new_path_str, context.get_deadline());
        }
        auto modified = from_unix(modification_s);
        bfs::last_write_time(new_path, modified, ec);
        return true;
    }
    return false;
}
