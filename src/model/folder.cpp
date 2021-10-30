#include "folder.h"
#include "../db/utils.h"
#include "../db/prefix.h"
#include "structs.pb.h"
#include <spdlog.h>
#include "misc/error_code.h"


namespace syncspirit::model {

static const constexpr char prefix = (char)(db::prefix::folder);

outcome::result<folder_ptr_t> folder_t::create(std::string_view key, std::string_view data) noexcept {
    if (key.size() != data_length) {
        return make_error_code(error_code_t::invalid_folder_key_length);
    }
    if (key[0] != prefix) {
        return make_error_code(error_code_t::invalid_folder_prefix);
    }

    auto ptr = folder_ptr_t();
    ptr = new folder_t(key);
    auto r = ptr->assign_fields(data);
    if (!r) {
        return r.assume_error();
    }
    return outcome::success(std::move(ptr));
}

outcome::result<folder_ptr_t> folder_t::create(const uuid_t& uuid, std::string_view data) noexcept {
    auto ptr = folder_ptr_t();
    ptr = new folder_t(uuid);
    auto r = ptr->assign_fields(data);
    if (!r) {
        return r.assume_error();
    }
    return outcome::success(std::move(ptr));
}

folder_t::folder_t(std::string_view key_) noexcept {
    std::copy(key_.begin(), key_.end(), key);
}

folder_t::folder_t(const uuid_t& uuid) noexcept {
    key[0] = prefix;
    std::copy(uuid.begin(), uuid.end(), key + 1);
}


outcome::result<void> folder_t::assign_fields(std::string_view data) noexcept {
    db::Folder item;
    auto ok = item.ParseFromArray(data.data(), data.size());
    if (!ok) {
        return make_error_code(error_code_t::folder_deserialization_failure);
    }
    id = item.id();
    label = item.label();
    path = item.path();
    folder_type = (foldet_type_t)item.folder_type();
    rescan_interval = item.rescan_interval();
    pull_order = (pull_order_t) item.pull_order();
    watched = item.watched();
    read_only = item.read_only();
    ignore_permissions = item.ignore_permissions();
    ignore_delete = item.ignore_delete();
    disable_temp_indixes = item.disable_temp_indexes();
    paused = item.paused();
    return outcome::success();
}


void folder_t::add(const folder_info_ptr_t &folder_info) noexcept { folder_infos.put(folder_info); }

void folder_t::assign_cluster(const cluster_ptr_t &cluster_) noexcept { cluster = cluster_.get(); }

std::string folder_t::serialize() noexcept {
    db::Folder r;
    r.set_id(id);
    r.set_label(label);
    r.set_read_only(read_only);
    r.set_ignore_permissions(ignore_permissions);
    r.set_ignore_delete(ignore_delete);
    r.set_disable_temp_indexes(disable_temp_indixes);
    r.set_paused(paused);
    r.set_watched(watched);
    r.set_path(path.string());
    r.set_folder_type((db::FolderType)folder_type);
    r.set_pull_order((db::PullOrder)pull_order);
    r.set_rescan_interval(rescan_interval);
    return r.SerializeAsString();
}


bool folder_t::is_shared_with(const model::device_ptr_t &device) noexcept {
    for (auto &it : folder_infos) {
        if (it.item->get_device() == device.get()) {
            return true;
        }
    }
    return false;
}

#if 0
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
        auto &fi = *it.item;
        auto &d = *fi.get_device();
        auto &pd = *r.add_devices();
        pd.set_id(std::string(d.device_id.get_sha256()));
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
        auto& fi = it.item;
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

bool folder_t::update(const proto::Folder &remote) noexcept {
    for (int i = 0; i < remote.devices_size(); ++i) {
        auto &d = remote.devices(i);
        for (auto it : folder_infos) {
            auto &fi = it.item;
            if (fi->get_device()->device_id.get_sha256() == d.id()) {
                return fi->update(d);
            }
        }
    }
    return false;
}

void folder_t::update(local_file_map_t &local_files) noexcept {
    auto folder_info = folder_infos.by_device(device);
    assert(folder_info);
    folder_info->update(local_files);
}

folder_info_ptr_t folder_t::get_folder_info(const device_ptr_t &device) noexcept {
    return folder_infos.by_device(device);
}

proto::Index folder_t::generate() noexcept {
    proto::Index r;
    r.set_folder(std::string(get_id()));
    auto fi = get_folder_info(device);
    for (auto it : fi->get_file_infos()) {
        auto &file = *it.item;
        *r.add_files() = file.get();
    }
    return r;
}
#endif

template<> std::string_view get_index<0>(const folder_ptr_t& item) noexcept { return item->get_key(); }
template<> std::string_view get_index<1>(const folder_ptr_t& item) noexcept { return item->get_id(); }

folder_ptr_t folders_map_t::by_id(std::string_view id) noexcept {
    return get<1>(id);
}


}
