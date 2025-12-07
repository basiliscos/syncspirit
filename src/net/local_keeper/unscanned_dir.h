// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "presentation/presence.h"
#include <filesystem>

namespace syncspirit::net::local_keeper {

namespace bfs = std::filesystem;

struct unscanned_dir_t {
    unscanned_dir_t(bfs::path path_, presentation::presence_ptr_t presence_)
        : path(std::move(path_)), presence(std::move(presence_)) {}

    bfs::path path;
    presentation::presence_ptr_t presence;
};

} // namespace syncspirit::net::local_keeper
