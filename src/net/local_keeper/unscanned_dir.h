// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "child_ready.h"
#include "model/presence.h"

namespace syncspirit::net::local_keeper {

struct unscanned_dir_t {
    using child_ready_ptr_t = std::unique_ptr<child_ready_t>;
    using generation_t = child_info_t::generation_t;

    unscanned_dir_t(utils::path_t path_, model::presence_ptr_t presence_, utils::path_t single_child_,
                    generation_t generation_, bool recurse_, bool requires_refinement_)
        : path(std::move(path_)), presence(std::move(presence_)), single_child{std::move(single_child_)},
          generation{generation_}, recurse{recurse_ ? 1u : 0}, requires_refinement{requires_refinement_ ? 1u : 0} {}

    unscanned_dir_t(child_ready_t child_ready_, bool recurse_, bool requires_refinement_)
        : child_ready(new child_ready_t(std::move(child_ready_))), generation{child_ready->generation},
          recurse{recurse_ ? 1u : 0}, requires_refinement{requires_refinement_ ? 1u : 0} {
        path = child_ready->path.clone();
        presence = child_ready->self;
    }

    utils::path_t path;
    model::presence_ptr_t presence;
    utils::path_t single_child;
    child_ready_ptr_t child_ready;
    generation_t generation;
    unsigned recurse : 1;
    unsigned requires_refinement : 1;
};

} // namespace syncspirit::net::local_keeper
