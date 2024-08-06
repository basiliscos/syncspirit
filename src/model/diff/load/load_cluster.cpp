// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "load_cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::load;

auto load_cluster_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    return visitor(*this, custom);
}

auto load_cluster_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    return applicator_t::apply_impl(cluster);
}
