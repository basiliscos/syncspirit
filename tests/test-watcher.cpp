// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "test-watcher.h"
#include "utils/format.hpp"

using namespace syncspirit::test;

void test_watcher_t::on_watch(fs::message::watch_folder_t &msg) noexcept {
    auto &p = msg.payload;
    p.ec = {};
    LOG_DEBUG(log, "watching {}", p.path);
    watched_folders->emplace(std::make_pair(std::string(p.folder_id), p.path.clone()));
}

void test_watcher_t::on_unwatch(fs::message::unwatch_folder_t &msg) noexcept {
    auto &p = msg.payload;
    auto it = watched_folders->find(p.folder_id);
    if (it != watched_folders->end()) {
        LOG_DEBUG(log, "unwatching '{}'", it->second);
        p.ec = {};
        watched_folders->erase(it);
    } else {
        LOG_ERROR(log, "cannot unwatch folder '{}'", p.folder_id);
    }
}
