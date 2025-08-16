// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "load_cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::load;

load_cluster_t::load_cluster_t(std::size_t blocks_count_, std::size_t files_count_) noexcept
    : blocks_count{blocks_count_}, files_count{files_count_} {}

auto load_cluster_t::apply_impl(cluster_t &cluster, apply_controller_t &controller, void *custom) const noexcept
    -> outcome::result<void> {
    return applicator_t::apply_impl(cluster, controller, custom);
}

auto load_cluster_t::apply_forward(cluster_t &cluster, apply_controller_t &controller, void *custom) const noexcept
    -> outcome::result<void> {
    return controller.apply(*this, custom);
}

auto load_cluster_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    return visitor(*this, custom);
}
