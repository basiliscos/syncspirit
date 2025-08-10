// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "remove_blocks.h"

#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::modify;

auto remove_blocks_t::apply_impl(cluster_t &cluster, apply_controller_t &controller, void *custom) const noexcept
    -> outcome::result<void> {
    if (!keys.empty()) {
        LOG_TRACE(log, "applying remove_blocks_t, blocks = {}", keys.size());
        auto &blocks = cluster.get_blocks();
        for (auto &block_key : keys) {
            auto block_hash = utils::bytes_view_t(block_key.data() + 1, block_key.size() - 1);
            auto b = blocks.by_hash(block_hash);
            blocks.remove(b);
        }
    }
    return applicator_t::apply_sibling(cluster, controller, custom);
}

auto remove_blocks_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting remove_blocks_t");
    return visitor(*this, custom);
}
