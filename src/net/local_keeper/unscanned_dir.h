// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "child_info.h"
#include "presentation/presence.h"
#include <filesystem>
#include <memory>

namespace syncspirit::net::local_keeper {

namespace bfs = std::filesystem;

struct unscanned_dir_t {
    using dir_info_t = std::unique_ptr<child_info_t>;

    unscanned_dir_t(bfs::path path_, presentation::presence_ptr_t presence_)
        : path(std::move(path_)), presence(std::move(presence_)) {}

    unscanned_dir_t(child_info_t dir_info_) : dir_info(new child_info_t(std::move(dir_info_))) {
        path = dir_info->path;
        presence = dir_info->self;
    }

    bfs::path path;
    presentation::presence_ptr_t presence;
    dir_info_t dir_info;
};

} // namespace syncspirit::net::local_keeper
