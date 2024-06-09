#include "remove_blocks.h"

#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::modify;

remove_blocks_t::remove_blocks_t(keys_t blocks_) noexcept : removed_blocks{std::move(blocks_)} {}

auto remove_blocks_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    if (!removed_blocks.empty()) {
        LOG_TRACE(log, "applyging remove_blocks_t, blocks = {}", removed_blocks.size());
        auto &blocks = cluster.get_blocks();
        for (auto &block_key : removed_blocks) {
            auto block_hash = block_key.substr(1);
            auto b = blocks.get(block_hash);
            blocks.remove(b);
        }
    }
    return outcome::success();
}

auto remove_blocks_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting remove_blocks_t");
    return visitor(*this, custom);
}
