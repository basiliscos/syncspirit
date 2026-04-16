// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "child_info.h"
#include "presentation/presence.h"
#include <filesystem>
#include <memory>

namespace syncspirit::net::local_keeper {

namespace bfs = std::filesystem;

struct unscanned_dir_t {
    using dir_info_t = std::unique_ptr<child_info_t>;
    using generation_t = child_info_t::generation_t;

    unscanned_dir_t(bfs::path path_, presentation::presence_ptr_t presence_, bfs::path single_child_,
                    generation_t generation_, bool recurse_, bool requires_refinement_)
        : path(std::move(path_)), presence(std::move(presence_)), single_child{std::move(single_child_)},
          generation{generation_}, recurse{recurse_ ? 1u : 0}, requires_refinement{requires_refinement_ ? 1u : 0} {}

    unscanned_dir_t(child_info_t dir_info_, bool recurse_, bool requires_refinement_)
        : dir_info(new child_info_t(std::move(dir_info_))), generation{dir_info_.generation},
          recurse{recurse_ ? 1u : 0}, requires_refinement{requires_refinement_ ? 1u : 0} {
        path = dir_info->path;
        presence = dir_info->self;
    }

    bfs::path path;
    presentation::presence_ptr_t presence;
    bfs::path single_child;
    dir_info_t dir_info;
    generation_t generation;
    unsigned recurse : 1;
    unsigned requires_refinement : 1;
};

} // namespace syncspirit::net::local_keeper
