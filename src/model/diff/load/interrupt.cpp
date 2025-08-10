// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "interrupt.h"
#include "model/cluster.h"
#include "model/diff/apply_controller.h"

using namespace syncspirit::model::diff::load;

auto interrupt_t::apply_forward(cluster_t &cluster, apply_controller_t &controller, void *custom) const noexcept
    -> outcome::result<void> {
    return controller.apply(*this, cluster, custom);
}

auto interrupt_t::apply_impl(cluster_t &cluster, apply_controller_t &controller, void *custom) const noexcept
    -> outcome::result<void> {
    return applicator_t::apply_sibling(cluster, controller, custom);
}
