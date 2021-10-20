#include "ignored_folders.h"
#include "../../cluster.h"

using namespace syncspirit::model::diff::load;

void ignored_folders_t::apply(cluster_t &cluster) const noexcept {
    auto& map = cluster.get_ignored_folders();
    for(auto& pair:folders) {
        auto folder = ignored_folder_ptr_t(new ignored_folder_t(pair.key, pair.value));
        map.put(folder);
    }
}
