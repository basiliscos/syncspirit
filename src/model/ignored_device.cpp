#include "ignored_device.h"
#include "../db/prefix.h"

namespace syncspirit::model {

static const constexpr char prefix = (char)(db::prefix::ignored_device);

ignored_device_t::ignored_device_t(const device_id_t& device) noexcept {
    hash[0] = prefix;
    auto sha256 = device.get_sha256();
    std::copy(sha256.begin(), sha256.end(), hash + 1);
}

ignored_device_t::ignored_device_t(std::string_view key, std::string_view data) noexcept {
    assert(key.size() == data_length);
    assert(key[0] == prefix);
    assert(data.size() == 1);
    assert(data[0] == prefix);
    (void)data;
    std::copy(key.begin(), key.end(), hash);
}

std::string_view ignored_device_t::get_key() const noexcept {
    return std::string_view(hash, data_length);
}

std::string_view ignored_device_t::get_sha256() const noexcept {
    return std::string_view(hash + 1, digest_length);
}

std::string ignored_device_t::serialize() noexcept {
    char c = prefix;
    return std::string(&c, 1);
}


template<>
std::string_view get_index<0, ignored_device_ptr_t>(const ignored_device_ptr_t& item) noexcept {
    return item->get_sha256();
}


}
