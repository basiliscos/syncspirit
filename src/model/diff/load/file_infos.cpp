#include "file_infos.h"
#include "../../cluster.h"
#include "../../../db/prefix.h"

using namespace syncspirit::model::diff::load;

void file_infos_t::apply(cluster_t &cluster) const noexcept {
    static const constexpr char folder_info_prefix = (char)(db::prefix::folder_info);

    folder_infos_map_t all_fi;
    auto& folders = cluster.get_folders();
    for(auto f:folders) {
        for(auto fi:f.item->get_folder_infos()) {
            all_fi.put(fi.item);
        }
    }

    for(auto& pair:container) {
        auto key = pair.key;
        auto folder_info_uuid = key.substr(1, uuid_length);
        auto folder_info = all_fi.get(folder_info_uuid);
        assert(folder_info);

        auto fi = file_info_ptr_t(new file_info_t(key, pair.value, folder_info));
        auto& map = folder_info->get_file_infos();
        map.put(fi);
    }
}
