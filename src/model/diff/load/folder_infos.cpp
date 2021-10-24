#include "folder_infos.h"
#include "../../cluster.h"
#include "../../../db/prefix.h"

using namespace syncspirit::model::diff::load;

void folder_infos_t::apply(cluster_t &cluster) const noexcept {
    static const constexpr char folder_prefix = (char)(db::prefix::folder);
    static const constexpr char device_prefix = (char)(db::prefix::device);

    auto& folders = cluster.get_folders();
    auto& devices = cluster.get_devices();
    for(auto& pair:container) {
        auto key = pair.key;
        auto device_id = key.substr(1, device_id_t::digest_length);
        auto folder_uuid = key.substr(1 + device_id_t::digest_length, uuid_length);

        char device_key[device_id_t::data_length];
        device_key[0] = device_prefix;
        std::copy(device_id.begin(), device_id.end(), device_key+1);

        auto device = devices.get(std::string_view(device_key, device_id_t::data_length));
        assert(device);

        char folder_key[uuid_length + 1];
        folder_key[0] = folder_prefix;
        std::copy(folder_uuid.begin(), folder_uuid.end(), folder_key+1);
        auto folder = folders.get(std::string_view(folder_key, uuid_length + 1));
        assert(folder);

        auto& map = folder->get_folder_infos();
        auto fi = folder_info_ptr_t(new folder_info_t(key, pair.value, device, folder));
        map.put(fi);
    }
}
