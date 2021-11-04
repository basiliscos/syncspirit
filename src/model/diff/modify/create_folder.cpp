#include "create_folder.h"
#include "../../cluster.h"

using namespace syncspirit::model::diff::modify;

auto create_folder_t::apply(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto &folders = cluster.get_folders();
    auto uuid = cluster.next_uuid();
/*
    auto data
    auto folder_opt =
    folders.put(folder);
    folder->assign_cluster(cluster.get());
    folder->assign_device(device);
*/
}
