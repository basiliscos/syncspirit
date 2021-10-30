#include "blocks.h"
#include "../../cluster.h"

using namespace syncspirit::model::diff::load;

auto blocks_t::apply(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto& blocks_map = cluster.get_blocks();
    for(auto& pair:blocks) {
        auto block = block_info_t::create(pair.key, pair.value);
        if (block.has_error()) {
            return block.assume_error();
        }
        blocks_map.put(std::move(block.value()));
    }
    return outcome::success();
}
