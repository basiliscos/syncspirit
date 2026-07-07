// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "local_state_update.h"
#include "model/diff/apply_controller.h"
#include "model/diff/cluster_visitor.h"
#include "model/cluster.h"
#include "utils/format.hpp"

using namespace syncspirit::model::diff::local;

local_state_update_t::local_state_update_t(const cluster_t &cluster, model::connection_state_t state_) noexcept
    : state{state_} {
    auto prev = cluster.get_device()->get_state().get_connection_state();
    LOG_DEBUG(log, "local_state_update_t, {} -> {}", (int)prev, (int)state);
}

auto local_state_update_t::apply_impl(apply_controller_t &controller, void *custom) const noexcept
    -> outcome::result<void> {
    auto local_device = controller.get_cluster().get_device().get();
    local_device->update_state(state);
    return outcome::success();
}

auto local_state_update_t::apply_forward(apply_controller_t &controller, void *custom) const noexcept
    -> outcome::result<void> {
    return controller.apply(*this, custom);
}

auto local_state_update_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    return visitor(*this, custom);
}
