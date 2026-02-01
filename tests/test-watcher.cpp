// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "test-watcher.h"
#include <boost/nowide/convert.hpp>

using namespace syncspirit::test;
using boost::nowide::narrow;

void test_watcher_t::on_watch(fs::message::watch_folder_t &msg) noexcept {
    auto &p = msg.payload;
    p.ec = {};
    auto path_str = narrow(p.path.generic_wstring());
    LOG_DEBUG(log, "watching {}", path_str);
    folder_map[p.folder_id] = folder_info_t(p.path, std::move(path_str));
}

void test_watcher_t::on_unwatch(fs::message::unwatch_folder_t &msg) noexcept {
    auto &p = msg.payload;
    auto it = folder_map.find(p.folder_id);
    if (it != folder_map.end()) {
        LOG_DEBUG(log, "unwatching {}", it->second.path_str);
        p.ec = {};
        folder_map.erase(it);
    } else {
        LOG_ERROR(log, "cannot unwatch folder '{}'", p.folder_id);
    }
}
