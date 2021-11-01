#include "cluster_update.h"
#include "model/cluster.h"
#include "model/diff/diff_visitor.h"
#include <spdlog/spdlog.h>

using namespace syncspirit::model::diff::peer;

auto cluster_update_t::create(const cluster_t &cluster, const device_ptr_t &source, message_t message) noexcept -> outcome::result<cluster_diff_ptr_t> {
    auto ptr = cluster_diff_ptr_t();

    unknown_folders_t unknown;
    modified_folders_t updated;
    modified_folders_t reset;
    auto& folders = cluster.get_folders();
    auto& devices = cluster.get_devices();

    for (int i = 0; i < message->folders_size(); ++i) {
        auto &f = message->folders(i);
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
            if (device != source) {
                continue;
            }

            auto& folder_infos = folder->get_folder_infos();
            auto folder_info = folder_infos.by_device(source);

            auto update_info = update_info_t { f.id(), d };
            if (d.index_id() != folder_info->get_index()) {
                reset.emplace_back(update_info);
            } else if (d.max_sequence() > folder_info->get_max_sequence()) {
                updated.emplace_back(update_info);
            }
        }
    }

    ptr = new cluster_update_t(std::move(unknown), std::move(reset), std::move(updated));
    return outcome::success(std::move(ptr));
}

cluster_update_t::cluster_update_t(unknown_folders_t unknown_folders, modified_folders_t reset_folders_, modified_folders_t updated_folders_) noexcept:
unknown_folders{std::move(unknown_folders)}, reset_folders{std::move(reset_folders_)}, updated_folders{std::move(updated_folders_)} {

}


auto cluster_update_t::apply(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto& folders = cluster.get_folders();

    for(auto& info: updated_folders) {
        auto folder = folders.by_id(info.folder_id);
        assert(folder);
        auto folder_info = folder->get_folder_infos().by_device_id(info.device.id());
        spdlog::trace("cluster_update_t::apply folder = {}, index = {:#x}, max seq = {} -> {}", folder->get_label(),
                      folder_info->get_index(), folder_info->get_max_sequence(), info.device.max_sequence());
    }
    if (reset_folders.size()) {
        std::abort();
    }
    return outcome::success();
}

auto cluster_update_t::visit(diff_visitor_t &visitor) const noexcept -> outcome::result<void> {
    return visitor(*this);
}
