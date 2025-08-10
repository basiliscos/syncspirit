// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "add_blocks.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"
#include "utils/format.hpp"

using namespace syncspirit::model::diff::modify;

add_blocks_t::add_blocks_t(blocks_t blocks_) noexcept : blocks(std::move(blocks_)) {
    LOG_DEBUG(log, "add_blocks_t, count = {}", blocks.size());
}

auto add_blocks_t::apply_impl(cluster_t &cluster, apply_controller_t &controller, void *custom) const noexcept
    -> outcome::result<void> {
    auto &bm = cluster.get_blocks();
    for (const auto &b : blocks) {
        auto opt = block_info_t::create(b);
        if (!opt) {
            return opt.assume_error();
        }
        auto block = std::move(opt.assume_value());
        if (!bm.put(block, false)) {
            LOG_TRACE(log, "add_blocks_t, failed to insert block '{}', already exists", block->get_hash());
        }
    }
    return applicator_t::apply_sibling(cluster, controller, custom);
}

auto add_blocks_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting add_blocks_t");
    return visitor(*this, custom);
}
