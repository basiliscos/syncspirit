// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "blocks.h"
#include "model/cluster.h"
#include "model/diff/apply_controller.h"
#include "model/misc/error_code.h"

using namespace syncspirit::model::diff::load;

auto blocks_t::apply_forward(cluster_t &cluster, apply_controller_t &controller) const noexcept
    -> outcome::result<void> {
    return controller.apply(*this, cluster);
}

auto blocks_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept -> outcome::result<void> {
    auto &blocks_map = cluster.get_blocks();
    for (auto &pair : blocks) {
        auto data = pair.value;
        auto db = db::BlockInfo();
        auto ok = db.ParseFromArray(data.data(), data.size());
        if (!ok) {
            return make_error_code(error_code_t::block_deserialization_failure);
        }
        auto block = block_info_t::create(pair.key, db);
        if (block.has_error()) {
            return block.assume_error();
        }
        blocks_map.put(std::move(block.value()));
    }
    return applicator_t::apply_sibling(cluster, controller);
}
