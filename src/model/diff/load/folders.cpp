#include "folders.h"
#include "../../cluster.h"

using namespace syncspirit::model::diff::load;

auto folders_t::apply(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto& map = cluster.get_folders();
    for(auto& pair:folders) {
        auto option = folder_t::create(pair.key, pair.value);
        if (!option) {
            return option.assume_error();
        }
        auto& folder = option.value();
        map.put(folder);
        folder->assign_cluster(&cluster);
    }
    return outcome::success();
}
