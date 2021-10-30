#include "file_infos.h"
#include "../../cluster.h"
#include "../../../db/prefix.h"
#include "structs.pb.h"

using namespace syncspirit::model::diff::load;

auto file_infos_t::apply(cluster_t &cluster) const noexcept -> outcome::result<void> {
    static const constexpr char folder_info_prefix = (char)(db::prefix::folder_info);
    static const constexpr char block_prefix = (char)(db::prefix::block_info);

    folder_infos_map_t all_fi;
    auto& folders = cluster.get_folders();
    for(auto f:folders) {
        for(auto fi:f.item->get_folder_infos()) {
            all_fi.put(fi.item);
        }
    }

    auto& blocks = cluster.get_blocks();

    for(auto& pair:container) {
        auto key = pair.key;
        auto folder_info_uuid = key.substr(1, uuid_length);
        auto folder_info = all_fi.get(folder_info_uuid);
        assert(folder_info);

        auto data = pair.value;
        db::FileInfo db_fi;
        auto ok = db_fi.ParseFromArray(data.data(), data.size());
        assert(ok);

        auto option = file_info_t::create(key, &db_fi, folder_info);
        if (!option) {
            return option.assume_error();
        }
        auto& fi = option.assume_value();
        auto& map = folder_info->get_file_infos();
        map.put(fi);

        for(int i = 0; i < db_fi.blocks_size(); ++i){
            auto block_hash = db_fi.blocks(i);
            auto block = blocks.get(block_hash);
            assert(block);
            fi->add_block(block);
        }
    }
    return outcome::success();
}
