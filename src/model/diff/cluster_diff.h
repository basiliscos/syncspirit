// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include "model/misc/arc.hpp"
#include "utils/log.h"
#include <boost/outcome.hpp>

namespace syncspirit::model {
struct cluster_t;
};

namespace syncspirit::model::diff {

namespace outcome = boost::outcome_v2;

struct apply_controller_t;
struct cluster_diff_t;
struct cluster_visitor_t;
using cluster_diff_ptr_t = intrusive_ptr_t<cluster_diff_t>;

struct SYNCSPIRIT_API cluster_diff_t : arc_base_t<cluster_diff_t> {
    using visitor_t = cluster_visitor_t;
    using applicator_t = cluster_diff_t;

    static utils::logger_t get_log() noexcept;

    cluster_diff_t();
    cluster_diff_t(const cluster_diff_t &) = delete;
    cluster_diff_t(cluster_diff_t &&) = delete;
    virtual ~cluster_diff_t() = default;

    outcome::result<void> apply(cluster_t &, apply_controller_t &) const noexcept;
    virtual outcome::result<void> visit(visitor_t &visitor, void *custom) const noexcept;
    outcome::result<void> visit_next(visitor_t &visitor, void *custom) const noexcept;

    cluster_diff_t *assign_sibling(cluster_diff_t *sibling) noexcept;
    cluster_diff_t *assign_child(cluster_diff_ptr_t child) noexcept;

    cluster_diff_ptr_t child;
    cluster_diff_ptr_t sibling;

  protected:
    virtual outcome::result<void> apply_impl(cluster_t &, apply_controller_t &) const noexcept;
    virtual outcome::result<void> apply_forward(cluster_t &, apply_controller_t &) const noexcept;
    outcome::result<void> apply_child(cluster_t &cluster, apply_controller_t &) const noexcept;
    outcome::result<void> apply_sibling(cluster_t &cluster, apply_controller_t &) const noexcept;

    utils::logger_t log;

    friend struct apply_controller_t;
};

// using cluster_visitor_t = cluster_diff_t::visitor_t;

} // namespace syncspirit::model::diff
