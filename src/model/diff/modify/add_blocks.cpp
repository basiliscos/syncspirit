// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "add_blocks.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::modify;

add_blocks_t::add_blocks_t(blocks_t blocks_) noexcept : blocks{std::move(blocks_)} {}

auto add_blocks_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto &bm = cluster.get_blocks();
    for (const auto &b : blocks) {
        auto opt = block_info_t::create(b);
        if (!opt) {
            return opt.assume_error();
        }
        auto block = std::move(opt.assume_value());
        bm.put(block);
    }
    return applicator_t::apply_sibling(cluster);
}

auto add_blocks_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting add_blocks_t");
    return visitor(*this, custom);
}
