#include "folders.h"
#include "../../cluster.h"

using namespace syncspirit::model::diff::load;

void folders_t::apply(cluster_t &cluster) const noexcept {
    auto& map = cluster.get_folders();
    for(auto& pair:folders) {
        auto folder = folder_ptr_t(new folder_t(pair.key, pair.value));
        map.put(folder);
        folder->assign_cluster(&cluster);
    }
}
