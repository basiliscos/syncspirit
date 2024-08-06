// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "generic_diff.hpp"
// #include "cluster_visitor.h"
#include "syncspirit-export.h"

namespace syncspirit::model::diff {

struct cluster_diff_t;
using cluster_diff_ptr_t = boost::intrusive_ptr<cluster_diff_t>;

struct SYNCSPIRIT_API cluster_diff_t : generic_diff_t<tag::cluster, cluster_diff_t> {
    using parent_t = generic_diff_t<tag::cluster, cluster_diff_t>;
    using visitor_t = parent_t::visitor_t;
    using parent_t::parent_t;
#if 0
    cluster_diff_t() noexcept = default;
    cluster_diff_t(cluster_diff_ptr_t child, cluster_diff_ptr_t sibling) noexcept;

    virtual outcome::result<void> visit(visitor_t &, void *custom) const noexcept override;

    cluster_diff_t *assign_sibling(cluster_diff_t *sibling) noexcept;
    void assign_child(cluster_diff_ptr_t child) noexcept;

    cluster_diff_ptr_t child;
    cluster_diff_ptr_t sibling;
#endif
};

using cluster_visitor_t = cluster_diff_t::visitor_t;

} // namespace syncspirit::model::diff
