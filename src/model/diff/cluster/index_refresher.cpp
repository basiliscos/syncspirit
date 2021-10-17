#include "index_refresher.h"
#include "../../cluster.h"

using namespace syncspirit::model::diff::cluster;

void index_refresher_t::apply(cluster_t &cluster) const noexcept {
#if 0
    auto folder = cluster.get_folders().by_id(folder_id);
    assert(folder);
    auto ex_folder_info = folder->get_folder_infos().by_id(device_id);
    assert(ex_folder_info);
    ex_folder_info->remove();

    db::FolderInfo db;
    db.set_max_sequence(device.max_sequence());
    db.set_index_id(device.index_id());

    auto d = ex_folder_info->get_device();
    auto new_folder_info = folder_info_ptr_t(new folder_info_t(db, d, folder.get(), 0));

    zzz;
#endif
    std::abort();

}
