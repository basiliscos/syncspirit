// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "fs/watcher_actor.h"
#include "syncspirit-test-export.h"

namespace syncspirit::test {

struct SYNCSPIRIT_TEST_API test_watcher_t : fs::watch_actor_t {
    using parent_t = fs::watch_actor_t;
    using parent_t::parent_t;

    void on_watch(fs::message::watch_folder_t &msg) noexcept override;
};

} // namespace syncspirit::test
