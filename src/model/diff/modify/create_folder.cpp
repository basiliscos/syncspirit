#include "create_folder.h"
#include "../../cluster.h"
#include "../../misc/error_code.h"

using namespace syncspirit::model::diff::modify;

auto create_folder_t::apply(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto &folders = cluster.get_folders();
    auto prev_folder = folders.by_id(item.id());
    if (prev_folder) {
        return make_error_code(error_code_t::folder_already_exists);
    }

    auto uuid = cluster.next_uuid();
    auto folder_opt = folder_t::create(uuid, item);
    if (!folder_opt) {
        return folder_opt.assume_error();
    }
    auto& folder = folder_opt.value();

    auto& my_device = cluster.get_device();
    db::FolderInfo db_fi_my;
    db_fi_my.set_index_id(cluster.next_uint64());

    auto fi_my_opt = folder_info_t::create(cluster.next_uuid(), db_fi_my, my_device, folder);
    if (!fi_my_opt) {
        return fi_my_opt.assume_error();
    }
    auto& fi_my = fi_my_opt.value();

    auto fi_source = folder_info_ptr_t();
    if (!source_device.empty()) {
        auto& devices = cluster.get_devices();
        auto source = devices.by_sha256(source_device);
        if (!source) {
            return make_error_code(error_code_t::source_device_not_exists);
        }
        db::FolderInfo db_fi_source;
        db_fi_source.set_index_id(source_index);
        db_fi_source.set_max_sequence(source_max_sequence);

        auto fi_source_opt = folder_info_t::create(cluster.next_uuid(), db_fi_source, source, folder);
        if (!fi_source_opt) {
            return fi_source_opt.assume_error();
        }
        fi_source = std::move(fi_source_opt.value());
    }

    auto& folder_infos = folder->get_folder_infos();
    folder_infos.put(fi_my);
    if (fi_source) {
        folder_infos.put(fi_source);
    }
    folders.put(folder);
    folder->assign_cluster(&cluster);

    return outcome::success();
}
