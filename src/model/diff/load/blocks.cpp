#include "blocks.h"
#include "../../cluster.h"

using namespace syncspirit::model::diff::load;

void blocks_t::apply(cluster_t &cluster) const noexcept {
    auto& blocks_map = cluster.get_blocks();
    for(auto& pair:blocks) {
        auto block = block_info_ptr_t(new block_info_t(pair.key, pair.value));
        blocks_map.put(block);
    }
}
