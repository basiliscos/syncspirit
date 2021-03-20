#include "folder.h"
#include <spdlog.h>

using namespace syncspirit;
using namespace syncspirit::model;

folder_t::folder_t(const db::Folder &db_folder, uint64_t db_key_) noexcept
    : db_key{db_key_}, _id{db_folder.id()}, _label{db_folder.label()}, path{db_folder.path()},
      folder_type{db_folder.folder_type()}, rescan_interval{static_cast<uint32_t>(db_folder.rescan_interval())},
      pull_order{db_folder.pull_order()}, watched{db_folder.watched()}, read_only{db_folder.read_only()},
      ignore_permissions{db_folder.ignore_permissions()}, ignore_delete{db_folder.ignore_delete()},
      disable_temp_indixes{db_folder.disable_temp_indexes()}, paused{db_folder.paused()} {}

#if 0
bool folder_t::assign(const proto::Folder &source, const devices_map_t &devices_map) noexcept {
    // remove outdated
    bool changed = false;
    for (auto it = devices.begin(); it != devices.end();) {
        bool has = false;
        for (int i = 0; i < source.devices_size(); ++i) {
            if (it->device->device_id.get_sha256() == source.devices(i).id()) {
                has = true;
                break;
            }
        }
        ++it;
    }
    // append possibly new
    for (int i = 0; i < source.devices_size(); ++i) {
        auto &d = source.devices(i);
        auto &raw_id = d.id();
        if (raw_id == device->device_id.get_sha256()) {
            continue;
        }
        auto device_id_option = device_id_t::from_sha256(raw_id);
        if (!device_id_option) {
            spdlog::warn("load_folder, cannot obtain device id from digest: {}", raw_id);
            continue;
        }
        auto &device_id = device_id_option.value().get_value();
        auto device = devices_map.by_id(device_id);
        if (!device) {
            spdlog::warn("load_folder, unknown device {}, ignoring", device_id);
            continue;
        }
        auto r = devices.emplace(model::folder_device_t{device, d.index_id(), d.max_sequence()});
        changed |= r.second;
    }
    return changed;
}

void folder_t::assing_self(index_id_t index, sequence_id_t max_sequence) noexcept {
    auto e_r = devices.emplace(model::folder_device_t{device, index, max_sequence});
    assert(e_r.second);
    (void)e_r;
}
#endif

void folder_t::add(const folder_info_ptr_t &folder_info) noexcept { folder_infos.put(folder_info); }

void folder_t::assign_device(model::device_ptr_t device_) noexcept { device = device_; }

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

std::optional<proto::Folder> folder_t::get(model::device_ptr_t device) noexcept {
    bool has = false;
    for (auto &it : folder_infos) {
        if (it.second->get_device() == device.get()) {
            has = true;
            break;
        }
    }
    if (!has) {
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
        proto::Device pd;
        pd.set_id(d.device_id.get_sha256());
        pd.set_name(d.name);
        pd.set_compression(d.compression);
        if (d.cert_name) {
            pd.set_cert_name(d.cert_name.value());
        }
        auto db_max_seq = fi.get_max_sequence();
        std::int64_t max_seq = paused ? 0 : (db_max_seq == 0 ? 1 : db_max_seq);
        pd.set_max_sequence(max_seq);
        pd.set_index_id(fi.get_index());
        pd.set_introducer(d.introducer);
        pd.set_skip_introduction_removals(d.skip_introduction_removals);
        *r.add_devices() = pd;
    }
    return r;
}

#if 0
proto::Folder folder_t::get() noexcept {
    proto::Folder r;
    r.set_id(_id);
    r.set_label(label);
    r.set_read_only(read_only);
    r.set_ignore_permissions(ignore_permissions);
    r.set_ignore_delete(ignore_delete);
    r.set_disable_temp_indexes(disable_temp_indixes);
    r.set_paused(paused);
    for (auto &fd : this->devices) {
        if (fd.device != this->device) {
            // zzz ?
            continue;
        }
        auto &id = fd.device->device_id.get_sha256();
        proto::Device pd;
        auto &device = fd.device;
        pd.set_id(id);
        pd.set_name(device->name);
        pd.set_compression(compression(device));
        if (device->cert_name) {
            pd.set_cert_name(device->cert_name.value());
        }
        spdlog::warn("zzz, folder {} has {} for {}", label, fd.max_sequence, fd.device->device_id);
        pd.set_max_sequence(0);
        //pd.set_max_sequence(fd.max_sequence);
        pd.set_introducer(device->introducer);
        pd.set_index_id(fd.index_id);
        pd.set_skip_introduction_removals(device->skip_introduction_removals);
        *r.add_devices() = pd;
    }
    return r;
}
#endif

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
            peer_seq = fi->get_declared_max_sequence();
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
                fi->update_declared_max_sequence(d.max_sequence());
            }
        }
    }
}
