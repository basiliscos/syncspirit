// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "remove_blocks.h"

#include "model/cluster.h"
#include "model/diff/apply_controller.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::modify;

auto remove_blocks_t::apply_impl(apply_controller_t &controller, void *custom) const noexcept -> outcome::result<void> {
    if (!keys.empty()) {
        LOG_TRACE(log, "applying remove_blocks_t, blocks = {}", keys.size());
        auto &cluster = controller.get_cluster();
        auto &blocks = cluster.get_blocks();
        for (auto &block_hash : keys) {
            auto b = blocks.by_hash(block_hash);
            blocks.remove(b);
        }
    }
    return applicator_t::apply_sibling(controller, custom);
}

auto remove_blocks_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting remove_blocks_t");
    return visitor(*this, custom);
}
