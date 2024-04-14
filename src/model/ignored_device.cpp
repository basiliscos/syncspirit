// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "ignored_device.h"
#include "../db/prefix.h"
#include "misc/error_code.h"

namespace syncspirit::model {

static const constexpr char prefix = (char)(db::prefix::ignored_device);

outcome::result<ignored_device_ptr_t> ignored_device_t::create(const device_id_t &device_id) noexcept {
    auto ptr = ignored_device_ptr_t();
    ptr = new ignored_device_t(device_id);
    return outcome::success(std::move(ptr));
}

outcome::result<ignored_device_ptr_t> ignored_device_t::create(std::string_view key, std::string_view data) noexcept {
    if (key.size() != data_length) {
        return make_error_code(error_code_t::invalid_ignored_device_key_length);
    }
    if (key[0] != prefix) {
        return make_error_code(error_code_t::invalid_ignored_device_prefix);
    }

    if ((data.size() != 1) || (data[0] != prefix)) {
        return make_error_code(error_code_t::ignored_device_deserialization_failure);
    }
    auto ptr = ignored_device_ptr_t();
    ptr = new ignored_device_t(key);
    return outcome::success(std::move(ptr));
}

ignored_device_t::ignored_device_t(const device_id_t &device) noexcept {
    hash[0] = prefix;
    auto sha256 = device.get_sha256();
    std::copy(sha256.begin(), sha256.end(), hash + 1);
}

ignored_device_t::ignored_device_t(std::string_view key) noexcept { std::copy(key.begin(), key.end(), hash); }

std::string_view ignored_device_t::get_key() const noexcept { return std::string_view(hash, data_length); }

std::string_view ignored_device_t::get_sha256() const noexcept { return std::string_view(hash + 1, digest_length); }

std::string ignored_device_t::serialize() noexcept {
    char c = prefix;
    return std::string(&c, 1);
}

template <>
SYNCSPIRIT_API std::string_view get_index<0, ignored_device_ptr_t>(const ignored_device_ptr_t &item) noexcept {
    return item->get_sha256();
}

} // namespace syncspirit::model
