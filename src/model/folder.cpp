#include "folder.h"
#include "../db/utils.h"
#include <spdlog.h>

using namespace syncspirit;
using namespace syncspirit::model;

folder_t::folder_t(const db::Folder &db_folder, uint64_t db_key_) noexcept
    : db_key{db_key_}, _id{db_folder.id()}, _label{db_folder.label()}, path{db_folder.path()},
      folder_type{db_folder.folder_type()}, rescan_interval{static_cast<uint32_t>(db_folder.rescan_interval())},
      pull_order{db_folder.pull_order()}, watched{db_folder.watched()}, read_only{db_folder.read_only()},
      ignore_permissions{db_folder.ignore_permissions()}, ignore_delete{db_folder.ignore_delete()},
      disable_temp_indixes{db_folder.disable_temp_indexes()}, paused{db_folder.paused()} {}

void folder_t::add(const folder_info_ptr_t &folder_info) noexcept { folder_infos.put(folder_info); }

void folder_t::assign_device(model::device_ptr_t device_) noexcept { device = device_; }

void folder_t::assign_cluster(cluster_t *cluster_) noexcept { cluster = cluster_; }

db::Folder folder_t::serialize() noexcept {
    db::Folder r;
    r.set_id(_id);
    r.set_label(_label);
    r.set_read_only(read_only);
    r.set_ignore_permissions(ignore_permissions);
    r.set_ignore_delete(ignore_delete);
    r.set_disable_temp_indexes(disable_temp_indixes);
    r.set_paused(paused);
    r.set_watched(watched);
    r.set_path(path.string());
    r.set_folder_type(folder_type);
    r.set_pull_order(pull_order);
    r.set_rescan_interval(rescan_interval);
    return r;
}

ignored_folder_t::ignored_folder_t(const db::IgnoredFolder &folder) noexcept : id{folder.id()}, label(folder.label()) {}

db::IgnoredFolder ignored_folder_t::serialize() const noexcept {
    db::IgnoredFolder r;
    r.set_id(id);
    r.set_label(label);
    return r;
    ;
}

bool folder_t::is_shared_with(const model::device_ptr_t &device) noexcept {
    for (auto &it : folder_infos) {
        if (it.second->get_device() == device.get()) {
            return true;
        }
    }
    return false;
}

std::optional<proto::Folder> folder_t::get(model::device_ptr_t device) noexcept {
    if (!is_shared_with(device)) {
        return {};
    }

    proto::Folder r;
    r.set_id(_id);
    r.set_label(_label);
    r.set_read_only(read_only);
    r.set_ignore_permissions(ignore_permissions);
    r.set_ignore_delete(ignore_delete);
    r.set_disable_temp_indexes(disable_temp_indixes);
    r.set_paused(paused);
    for (auto &it : folder_infos) {
        auto &fi = *it.second;
        auto &d = *fi.get_device();
        auto &pd = *r.add_devices();
        pd.set_id(d.device_id.get_sha256());
        pd.set_name(d.name);
        pd.set_compression(d.compression);
        if (d.cert_name) {
            pd.set_cert_name(d.cert_name.value());
        }
        auto db_max_seq = fi.get_max_sequence();
        std::int64_t max_seq = db_max_seq;
        pd.set_max_sequence(max_seq);
        pd.set_index_id(fi.get_index());
        pd.set_introducer(d.introducer);
        pd.set_skip_introduction_removals(d.skip_introduction_removals);
        spdlog::trace("folder_t::get (==>), folder = {}/{:#x}, device = {}, max_seq = {}", _label, fi.get_index(),
                      d.device_id, max_seq);
    }
    return r;
}

int64_t folder_t::score(const device_ptr_t &peer_device) noexcept {
    std::int64_t r = 0;
    std::int64_t my_seq = 0;
    std::int64_t peer_seq = 0;
    for (auto it : folder_infos) {
        auto fi = it.second;
        auto &d = *fi->get_device();
        if (d == *device) {
            my_seq = fi->get_max_sequence();
        } else if (d == *peer_device) {
            peer_seq = fi->get_max_sequence();
        }
        if (my_seq && peer_seq) {
            break;
        }
    }
    if (peer_seq > my_seq) {
        return peer_seq - my_seq;
    }
    return r;
}

void folder_t::update(const proto::Folder &remote) noexcept {
    for (int i = 0; i < remote.devices_size(); ++i) {
        auto &d = remote.devices(i);
        for (auto it : folder_infos) {
            auto &fi = it.second;
            if (fi->get_device()->device_id.get_sha256() == d.id()) {
                fi->update(d);
            }
        }
    }
}

void folder_t::update(local_file_map_t &local_files) noexcept {
    auto folder_info = folder_infos.by_id(device->get_id());
    assert(folder_info);
    folder_info->update(local_files);
}

folder_info_ptr_t folder_t::get_folder_info(const device_ptr_t &device) noexcept {
    return folder_infos.by_id(device->get_id());
}
