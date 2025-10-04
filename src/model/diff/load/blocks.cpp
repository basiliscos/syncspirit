// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "blocks.h"
#include "model/cluster.h"
#include "model/diff/apply_controller.h"

using namespace syncspirit::model::diff::load;

auto blocks_t::apply_forward(apply_controller_t &controller, void *custom) const noexcept -> outcome::result<void> {
    return controller.apply(*this, custom);
}

auto blocks_t::apply_impl(apply_controller_t &controller, void *custom) const noexcept -> outcome::result<void> {
    auto &cluster = controller.get_cluster();
    auto &blocks_map = cluster.get_blocks();
    for (auto &pair : blocks) {
        auto block = block_info_t::create(pair.key, pair.db_block);
        if (block.has_error()) {
            return block.assume_error();
        }
        blocks_map.put(std::move(block.value()));
    }
    return applicator_t::apply_sibling(controller, custom);
}
