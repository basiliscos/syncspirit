#include "folder_info.h"
#include "folder.h"
#include "structs.pb.h"
#include "../db/prefix.h"
#include "misc/error_code.h"
#include <spdlog.h>

namespace syncspirit::model {

static const constexpr char prefix = (char)(db::prefix::folder_info);

outcome::result<folder_info_ptr_t> folder_info_t::create(std::string_view key, std::string_view data, const device_ptr_t& device_, const folder_ptr_t& folder_) noexcept {
    if (key.size() != data_length) {
        return make_error_code(error_code_t::invalid_folder_info_key_length);
    }
    if (key[0] != prefix) {
        return make_error_code(error_code_t::invalid_folder_info_prefix);
    }

    auto ptr = folder_info_ptr_t();
    ptr = new folder_info_t(key, device_, folder_);

    auto r = ptr->assign_fields(data);
    if (!r) {
        return r.assume_error();
    }

    return outcome::success(std::move(ptr));
}

outcome::result<folder_info_ptr_t> folder_info_t::create(const uuid_t& uuid, std::string_view data, const device_ptr_t& device_, const folder_ptr_t& folder_) noexcept {
    auto ptr = folder_info_ptr_t();
    ptr = new folder_info_t(uuid, device_, folder_);

    auto r = ptr->assign_fields(data);
    if (!r) {
        return r.assume_error();
    }

    return outcome::success(std::move(ptr));
}


folder_info_t::folder_info_t(std::string_view key_, const device_ptr_t& device_, const folder_ptr_t& folder_) noexcept:
    device{device_.get()}, folder{folder_.get()} {
    assert(key_.substr(1, device_id_t::digest_length) == device->get_key().substr(1));
    assert(key_.substr(device_id_t::digest_length + 1, uuid_length) == folder->get_key().substr(1));
    std::copy(key_.begin(), key_.end(), key);
}

folder_info_t::folder_info_t(const uuid_t& uuid, const device_ptr_t &device_, const folder_ptr_t &folder_) noexcept:
    device{device_.get()}, folder{folder_.get()} {
    auto device_key = device->get_key().substr(1);
    auto folder_key = folder->get_key().substr(1);
    key[0] = prefix;
    std::copy(device_key.begin(), device_key.end(), key + 1);
    std::copy(folder_key.begin(), folder_key.end(), key + 1 + device_key.size());
    std::copy(uuid.begin(), uuid.end(), key + 1 + device_key.size() + folder_key.size());
}

folder_info_t::~folder_info_t() {}

outcome::result<void> folder_info_t::assign_fields(std::string_view data) noexcept {
    db::FolderInfo fi;
    auto ok = fi.ParseFromArray(data.data(), data.size());
    if (!ok) {
        return make_error_code(error_code_t::folder_info_deserialization_failure);
    }
    index = fi.index_id();
    max_sequence = fi.max_sequence();
    return outcome::success();
}

std::string_view folder_info_t::get_key() noexcept {
    return std::string_view(key, data_length);
}


std::string_view folder_info_t::get_uuid() noexcept {
    return std::string_view(key + 1 + device_id_t::digest_length + uuid_length, uuid_length);
}


bool folder_info_t::operator==(const folder_info_t &other) const noexcept {
    auto r = std::mismatch(key, key + data_length, other.key);
    return r.first == key + data_length;
}


void folder_info_t::add(const file_info_ptr_t &file_info) noexcept {
    file_infos.put(file_info);
    mark_dirty();
}

#if 0
bool folder_info_t::update(const proto::Device &device) noexcept {
    bool update = false;
    bool new_index = false;
    if (index != device.index_id()) {
        index = device.index_id();
        new_index = true;
    }
    if (max_sequence != device.max_sequence()) {
        max_sequence = device.max_sequence();
        update = true;
    }
    if (update || new_index) {
        spdlog::trace("folder_info_t::update, folder = {}, index = {:#x}, max seq = {}", folder->get_label(), index,
                      max_sequence);
        mark_dirty();
    }
    return new_index;
}
#endif

void folder_info_t::set_max_sequence(int64_t value) noexcept {
    assert(max_sequence < value);
    max_sequence = value;
    mark_dirty();
}

std::string folder_info_t::serialize() noexcept {
    db::FolderInfo r;
    r.set_index_id(index);
    r.set_max_sequence(max_sequence);
    return r.SerializeAsString();
}

template <typename Message> void folder_info_t::update_generic(const Message &data, const device_ptr_t &peer) noexcept {
    std::abort();
#if 0
    std::int64_t max_sequence = get_max_sequence();
    for (int i = 0; i < data.files_size(); ++i) {
        auto &file = data.files(i);
        auto seq = file.sequence();
        spdlog::trace("folder_info_t::update, folder = {}, device = {}, file = {}, seq = {}", folder->label(),
                      device->device_id, file.name(), seq);

        auto file_key = file_info_t::generate_db_key(file.name(), *this);
        auto fi = file_infos.by_key(file_key);
        if (fi) {
            fi->update(file);
        } else {
            fi = file_info_ptr_t(new file_info_t(file, this));
            add(fi);
            mark_dirty();
        }
        if (seq > max_sequence) {
            max_sequence = seq;
        }
    }
    if (get_max_sequence() < max_sequence) {
        this->max_sequence = max_sequence;
        mark_dirty();
    }

    spdlog::debug("folder_info_t::update, folder_info = {} max seq = {}, device = {}", get_db_key(), max_sequence,
                  peer->device_id);
    /*
    auto local_folder_info = folder_infos.by_id(device->device_id.get_sha256());
    if (local_folder_info->get_max_sequence() < max_sequence) {
        local_folder_info->set_max_sequence(max_sequence);
        spdlog::trace("folder_t::update, folder_info = {} max seq = {}, device = {} (local)",
                      local_folder_info->get_db_key(), max_sequence, device->device_id);
    }
    */
#endif
}

void folder_info_t::update(const proto::IndexUpdate &data, const device_ptr_t &peer) noexcept {
    update_generic(data, peer);
}

void folder_info_t::update(const proto::Index &data, const device_ptr_t &peer) noexcept { update_generic(data, peer); }

#if 0
void folder_info_t::update(local_file_map_t &local_files) noexcept {
    std::abort();
    auto file_infos_copy = file_infos;
    for (auto it : local_files.map) {
        auto file_key = file_info_t::generate_db_key(it.first.string(), *this);
        auto cluster_file = file_infos_copy.by_key(file_key);
        if (cluster_file) {
            auto updated = cluster_file->update(it.second);
            file_infos_copy.remove(cluster_file);
            if (updated) {
                std::abort();
            }
        }
    }
    for (auto it : file_infos_copy) {
        auto &file = it.second;
        if (file->is_deleted()) {
            // no-op, file is deleted in local index and does not present in filesystem
        } else {
#if 0
            auto key = file->get_path();
            auto &local_file = local_files.map.at(key);
#endif
            std::abort();
        }
    }
}
#endif

folder_info_ptr_t folder_infos_map_t::by_device(const device_ptr_t& device) noexcept {
    return get<1>(device->device_id().get_sha256());
}

folder_info_ptr_t folder_infos_map_t::by_device_id(std::string_view device_id) noexcept {
    return get<1>(device_id);
}


template<> std::string_view get_index<0>(const folder_info_ptr_t& item) noexcept { return item->get_uuid(); }
template<> std::string_view get_index<1>(const folder_info_ptr_t& item) noexcept {
    return item->get_device()->device_id().get_sha256();
}

} // namespace syncspirit::model
