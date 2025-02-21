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
    name = db.name();
    client_name = db.client_name();
    client_version = db.client_version();
    address = db.address();
    last_seen = pt::from_time_t(db.last_seen());
}

syncspirit::utils::bytes_t some_device_base_t::serialize() noexcept {
    db::SomeDevice db;
    serialize(db);
    return db.encode();
}

void some_device_base_t::serialize(db::SomeDevice &db) const noexcept {
    db.name(name);
    db.client_name(client_name);
    db.client_version(client_version);
    db.address(address);
    db.last_seen(utils::as_seconds(last_seen));
}

std::string_view some_device_base_t::get_name() const noexcept { return name; }
std::string_view some_device_base_t::get_client_name() const noexcept { return client_name; }
std::string_view some_device_base_t::get_client_version() const noexcept { return client_version; }
std::string_view some_device_base_t::get_address() const noexcept { return address; }
const pt::ptime &some_device_base_t::get_last_seen() const noexcept { return last_seen; }
void some_device_base_t::set_last_seen(const pt::ptime &value) noexcept { last_seen = value; }
