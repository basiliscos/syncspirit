// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "remove_file.h"
#include "fs/messages.h"
#include "fs/updates_mediator.h"
#include <boost/nowide/convert.hpp>

using namespace syncspirit::fs::task;
using boost::nowide::narrow;

remove_file_t::remove_file_t(bfs::path path_) noexcept : path{std::move(path_)} {}

bool remove_file_t::process(fs_slave_t &fs_slave, execution_context_t &context) noexcept {
    ec = {};
    bfs::remove(path, ec);
    if (!ec) {
        if (auto mediator = context.mediator; mediator) {
            auto path_str = narrow(path.generic_wstring());
            mediator->push(std::move(path_str), {}, context.get_deadline());
        }
        return true;
    }
    return false;
}
