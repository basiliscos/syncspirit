// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "remove_file.h"

using namespace syncspirit::fs::task;

remove_file_t::remove_file_t(bfs::path path_) noexcept : path{std::move(path_)} {}

void remove_file_t::process(fs_slave_t &fs_slave, hasher::hasher_plugin_t *) noexcept {
    ec = {};
    bfs::remove(path, ec);
}
