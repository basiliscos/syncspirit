#include "max_sequence_updater.h"
#include "../../cluster.h"

using namespace syncspirit::model::diff::cluster;

void max_sequence_updater_t::apply(cluster_t &cluster) const noexcept {
#if 0
    auto folder = cluster.get_folders().by_id(folder_id);
    assert(folder);
    auto folder_info = folder->get_folder_infos().by_id(device_id);
    assert(folder_info);
    folder_info->set_max_sequence(max_sequence);
#endif
    std::abort();
}
