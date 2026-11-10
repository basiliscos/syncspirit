// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#pragma once

#include "task.h"
#include "child_info.h"
#include "model/presence.h"
#include "net/local_keeper/child_ready.h"

namespace syncspirit::fs::task {

struct SYNCSPIRIT_API scan_dir_t {
    using child_infos_t = std::vector<child_info_t>;
    using child_ready_t = net::local_keeper::child_ready_t;

    scan_dir_t(utils::path_t path, model::presence_ptr_t presence, utils::path_t single_child
               , child_ready_t child_ready, bool notify,
               bool recurse, bool requires_refinement) noexcept;
    bool process(fs_slave_t &fs_slave, execution_context_t &context) noexcept;

    utils::path_t path;
    model::presence_ptr_t presence;
    std::error_code ec;
    child_infos_t child_infos;
    utils::path_t single_child;
    child_ready_t child_ready;
    unsigned notify : 1;
    unsigned recurse : 1;
    unsigned requires_refinement : 1;
};

} // namespace syncspirit::fs::task
