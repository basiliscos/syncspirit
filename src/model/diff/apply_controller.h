// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include "diffs_fwd.h"
#include "model/misc/arc.hpp"
#include <boost/outcome.hpp>

namespace syncspirit::model {
struct cluster_t;
using cluster_ptr_t = intrusive_ptr_t<cluster_t>;
} // namespace syncspirit::model

namespace syncspirit::model::diff {

namespace outcome = boost::outcome_v2;

struct SYNCSPIRIT_API apply_controller_t {

    virtual ~apply_controller_t();

    virtual outcome::result<void> apply(const cluster_diff_t &, void *) noexcept;
    virtual outcome::result<void> apply(const advance::advance_t &, void *) noexcept;
    virtual outcome::result<void> apply(const load::blocks_t &, void *) noexcept;
    virtual outcome::result<void> apply(const load::commit_t &, void *r) noexcept;
    virtual outcome::result<void> apply(const load::file_infos_t &, void *) noexcept;
    virtual outcome::result<void> apply(const load::interrupt_t &, void *r) noexcept;
    virtual outcome::result<void> apply(const load::load_cluster_t &, void *) noexcept;
    virtual outcome::result<void> apply(const local::io_failure_t &, void *) noexcept;
    virtual outcome::result<void> apply(const local::scan_start_t &, void *) noexcept;
    virtual outcome::result<void> apply(const modify::add_pending_folders_t &, void *) noexcept;
    virtual outcome::result<void> apply(const modify::add_pending_device_t &, void *) noexcept;
    virtual outcome::result<void> apply(const modify::add_ignored_device_t &, void *) noexcept;
    virtual outcome::result<void> apply(const modify::update_peer_t &, void *) noexcept;
    virtual outcome::result<void> apply(const modify::upsert_folder_t &, void *) noexcept;
    virtual outcome::result<void> apply(const modify::upsert_folder_info_t &, void *) noexcept;
    virtual outcome::result<void> apply(const peer::update_folder_t &, void *) noexcept;

    inline model::cluster_t &get_cluster() noexcept { return *cluster; }

  protected:
    model::cluster_ptr_t cluster;
};

} // namespace syncspirit::model::diff
