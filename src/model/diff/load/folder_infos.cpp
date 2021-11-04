#include "folder_infos.h"
#include "../../misc/error_code.h"
#include "../../cluster.h"
#include "../../../db/prefix.h"

using namespace syncspirit::model::diff::load;

auto folder_infos_t::apply(cluster_t &cluster) const noexcept -> outcome::result<void> {
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
        if (!device) {
            return make_error_code(error_code_t::no_such_device);
        }

        char folder_key[uuid_length + 1];
        folder_key[0] = folder_prefix;
        std::copy(folder_uuid.begin(), folder_uuid.end(), folder_key+1);
        auto folder = folders.get(std::string_view(folder_key, uuid_length + 1));
        if (!folder) {
            return make_error_code(error_code_t::no_such_folder);
        }


        auto data = pair.value;
        db::FolderInfo db;
        auto ok = db.ParseFromArray(data.data(), data.size());
        if (!ok) {
            return make_error_code(error_code_t::folder_info_deserialization_failure);
        }

        auto& map = folder->get_folder_infos();
        auto option = folder_info_t::create(key, db, device, folder);
        if (!option) {
            return option.assume_error();
        }
        auto& fi = option.assume_value();
        map.put(fi);
    }
    return outcome::success();
}
