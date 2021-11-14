#include "cluster_update.h"
#include "model/cluster.h"
#include "model/diff/diff_visitor.h"
#include <spdlog/spdlog.h>

using namespace syncspirit::model::diff::peer;

auto cluster_update_t::create(const cluster_t &cluster, const device_t &source, const message_t &message) noexcept -> outcome::result<cluster_diff_ptr_t> {
    auto ptr = cluster_diff_ptr_t();

    unknown_folders_t unknown;
    modified_folders_t updated;
    modified_folders_t reset;
    keys_t removed_folders;
    auto& folders = cluster.get_folders();
    auto& devices = cluster.get_devices();

    for (int i = 0; i < message.folders_size(); ++i) {
        auto &f = message.folders(i);
        auto folder = folders.by_id(f.id());
        if (!folder) {
            unknown.emplace_back(f);
            continue;
        }

        for(int j = 0; j < f.devices_size(); ++j) {
            auto& d = f.devices(j);
            auto device_sha = d.id();
            auto device = devices.by_sha256(device_sha);
            assert(device);
            if (device != &source) {
                continue;
            }

            auto& folder_infos = folder->get_folder_infos();
            auto folder_info = folder_infos.by_device(device);

            auto update_info = update_info_t { f.id(), d };
            if (d.index_id() != folder_info->get_index()) {
                reset.emplace_back(update_info);
                removed_folders.emplace_back(folder_info->get_key());
                if (folder_info->get_file_infos().size() > 0) {
                    spdlog::critical("folder reset is not implemented");
                    std::abort();
                }
             } else if (d.max_sequence() > folder_info->get_max_sequence()) {
                updated.emplace_back(update_info);
            }
        }
    }

    ptr = new cluster_update_t(source.device_id().get_sha256(), std::move(unknown), std::move(reset), std::move(updated),
                               removed_folders, {}, {});
    return outcome::success(std::move(ptr));
}

cluster_update_t::cluster_update_t(std::string_view source_device_, unknown_folders_t unknown_folders, modified_folders_t reset_folders_,
                                   modified_folders_t updated_folders_, keys_t removed_folders_, keys_t removed_files_, keys_t removed_blocks_) noexcept:
unknown_folders{std::move(unknown_folders)}, reset_folders{std::move(reset_folders_)}, updated_folders{std::move(updated_folders_)},
  source_device{source_device_}, removed_folders{removed_folders_}, removed_files{removed_files_}, removed_blocks{removed_blocks_}
{

}


auto cluster_update_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "applyging cluster_update_t");
    auto& folders = cluster.get_folders();

    for(auto& info: updated_folders) {
        auto folder = folders.by_id(info.folder_id);
        assert(folder);
        auto folder_info = folder->get_folder_infos().by_device_id(info.device.id());
        spdlog::trace("cluster_update_t::apply folder = {}, index = {:#x}, max seq = {} -> {}", folder->get_label(),
                      folder_info->get_index(), folder_info->get_max_sequence(), info.device.max_sequence());
    }
    for(auto& info: reset_folders) {
        auto folder = folders.by_id(info.folder_id);
        assert(folder);
        auto& folder_infos = folder->get_folder_infos();
        auto folder_info = folder_infos.by_device_id(info.device.id());
        folder_infos.remove(folder_info);
        db::FolderInfo db_fi;
        db_fi.set_index_id(info.device.index_id());
        db_fi.set_max_sequence(info.device.max_sequence());
        auto opt = folder_info_t::create(cluster.next_uuid(), db_fi, folder_info->get_device(), folder);
        if (!opt) {
            return opt.assume_error();
        }
        folder_infos.put(opt.assume_value());
    }
    return outcome::success();
}

auto cluster_update_t::visit(diff_visitor_t &visitor) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting cluster_update_t");
    return visitor(*this);
}
