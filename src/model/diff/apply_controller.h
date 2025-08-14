// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include "diffs_fwd.h"
#include <boost/outcome.hpp>

namespace syncspirit::model {
struct cluster_t;
}

namespace syncspirit::model::diff {

namespace outcome = boost::outcome_v2;

struct SYNCSPIRIT_API apply_controller_t {
    virtual outcome::result<void> apply(const cluster_diff_t &, cluster_t &cluster, void *) noexcept;
    virtual outcome::result<void> apply(const load::blocks_t &, cluster_t &cluster, void *) noexcept;
    virtual outcome::result<void> apply(const load::commit_t &, cluster_t &cluster, void *r) noexcept;
    virtual outcome::result<void> apply(const load::file_infos_t &, cluster_t &cluster, void *) noexcept;
    virtual outcome::result<void> apply(const load::interrupt_t &, cluster_t &cluste, void *r) noexcept;
    virtual outcome::result<void> apply(const load::load_cluster_t &, cluster_t &cluster, void *) noexcept;
};

} // namespace syncspirit::model::diff
