#include "pending_folder.h"
#include "db/prefix.h"
#include "misc/error_code.h"

#ifdef uuid_t
#undef uuid_t
#endif

namespace syncspirit::model {

static const constexpr char prefix = (char)(syncspirit::db::prefix::unknown_folder);

outcome::result<unknown_folder_ptr_t> unknown_folder_t::create(std::string_view key,
                                                               const db::UnknownFolder &data) noexcept {
    if (key.size() != data_length) {
        return make_error_code(error_code_t::invalid_unknown_folder_length);
    }

    if (key[0] != prefix) {
        return make_error_code(error_code_t::invalid_folder_prefix);
    }

    auto sha256 = key.substr(uuid_length + 1);
    auto device = device_id_t::from_sha256(sha256);
    if (!device) {
        return make_error_code(error_code_t::malformed_deviceid);
    }

    auto ptr = unknown_folder_ptr_t();
    ptr = new unknown_folder_t(key, device.value());
    ptr->assign_fields(data);
    return outcome::success(std::move(ptr));
}

outcome::result<unknown_folder_ptr_t> unknown_folder_t::create(const uuid_t &uuid, const db::UnknownFolder &data,
                                                               const device_id_t &device_) noexcept {
    auto ptr = unknown_folder_ptr_t();
    ptr = new unknown_folder_t(uuid, device_);
    ptr->assign_fields(data);
    return outcome::success(std::move(ptr));
}

unknown_folder_t::unknown_folder_t(const uuid_t &uuid, const device_id_t &device_) noexcept : device{device_} {
    key[0] = prefix;
    std::copy(uuid.begin(), uuid.end(), key + 1);
    auto sha256 = device.get_sha256();
    std::copy(sha256.begin(), sha256.end(), key + 1 + uuid_length);
}

unknown_folder_t::unknown_folder_t(std::string_view key_, const device_id_t &device_) noexcept : device{device_} {
    std::copy(key_.begin(), key_.end(), key);
}

void unknown_folder_t::assign_fields(const db::UnknownFolder &data) noexcept {
    folder_data_t::assign_fields(data.folder());
    auto &fi = data.folder_info();
    index = fi.index_id();
    max_sequence = fi.max_sequence();
    id = data.folder().id();
}

std::string unknown_folder_t::serialize() const noexcept {
    db::UnknownFolder r;
    folder_data_t::serialize(*r.mutable_folder());
    auto &fi = *r.mutable_folder_info();
    fi.set_index_id(index);
    fi.set_max_sequence(max_sequence);
    return r.SerializePartialAsString();
}

template <> SYNCSPIRIT_API std::string_view get_index<0>(const unknown_folder_ptr_t &item) noexcept {
    return item->get_key();
}

template <> SYNCSPIRIT_API std::string_view get_index<1>(const unknown_folder_ptr_t &item) noexcept {
    return item->get_id();
}

unknown_folder_ptr_t unknown_folder_map_t::by_key(std::string_view key) const noexcept { return get<0>(key); }

unknown_folder_ptr_t unknown_folder_map_t::by_id(std::string_view id) const noexcept { return get<1>(id); }

} // namespace syncspirit::model
