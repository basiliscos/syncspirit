// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "some_device.h"
#include "proto/proto-structs.h"
#include "utils/time.h"

using namespace syncspirit::model;

some_device_base_t::some_device_base_t(char prefix, const device_id_t &device_id_, const db::SomeDevice &db) noexcept
    : device_id{device_id_} {
    auto sha256 = device_id.get_sha256();
    hash[0] = prefix;
    std::copy(sha256.begin(), sha256.end(), hash + 1);
    assign(db);
}

void some_device_base_t::assign(const db::SomeDevice &db) noexcept {
    name = db::get_name(db);
    client_name = db::get_client_name(db);
    client_version = db::get_client_version(db);
    address = db::get_address(db);
    last_seen = pt::from_time_t(db::get_last_seen(db));
}

syncspirit::utils::bytes_t some_device_base_t::serialize() noexcept {
    db::SomeDevice db;
    serialize(db);
    return db::encode::some_device(db);
}

void some_device_base_t::serialize(db::SomeDevice &db) const noexcept {
    db::set_name(db, name);
    db::set_client_name(db, client_name);
    db::set_client_version(db, client_version);
    db::set_address(db, address);
    db::set_last_seen(db, utils::as_seconds(last_seen));
}

std::string_view some_device_base_t::get_name() const noexcept { return name; }
std::string_view some_device_base_t::get_client_name() const noexcept { return client_name; }
std::string_view some_device_base_t::get_client_version() const noexcept { return client_version; }
std::string_view some_device_base_t::get_address() const noexcept { return address; }
const pt::ptime &some_device_base_t::get_last_seen() const noexcept { return last_seen; }
void some_device_base_t::set_last_seen(const pt::ptime &value) noexcept { last_seen = value; }
