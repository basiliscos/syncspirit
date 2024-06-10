#include "remove_files.h"

#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::modify;

remove_files_t::remove_files_t(const device_t &device, const file_infos_map_t &files) noexcept
    : device_id{device.device_id().get_sha256()} {
    keys.reserve(files.size());
    folder_ids.reserve(files.size());
    for (auto &it : files) {
        auto &file = it.item;
        folder_ids.push_back(std::string(file->get_folder_info()->get_folder()->get_id()));
        keys.push_back(std::string(file->get_key()));
    }
}

auto remove_files_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto &folders = cluster.get_folders();
    for (size_t i = 0; i < folder_ids.size(); ++i) {
        auto folder = cluster.get_folders().by_id(folder_ids[i]);
        auto &folder_infos = folder->get_folder_infos();
        auto folder_info = folder_infos.by_device_id(device_id);
        auto &file_infos = folder_info->get_file_infos();
        auto decomposed = file_info_t::decompose_key(keys[i]);
        auto file = file_infos.get(decomposed.file_id);
        file_infos.remove(file);
    }
    return outcome::success();
}

auto remove_files_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting remove_files_t");
    return visitor(*this, custom);
}
