// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include <boost/asio.hpp>
#include "misc/augmentation.hpp"
#include "misc/map.hpp"
#include "misc/error_code.h"

#include "device_id.h"
#include "syncspirit-export.h"
#include "structs.pb.h"
#include "utils/time.h"

#include <boost/outcome.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace syncspirit::model {

namespace outcome = boost::outcome_v2;
namespace pt = boost::posix_time;

template <char prefix> struct SYNCSPIRIT_API some_device_t final : augmentable_t<some_device_t<prefix>> {

    using ptr_t = intrusive_ptr_t<some_device_t>;

    some_device_t(const some_device_t &&other) = delete;

    static outcome::result<ptr_t> create(const device_id_t &id, const db::SomeDevice &db) noexcept {
        return ptr_t(new some_device_t(id, db));
    }

    std::string_view get_key() const noexcept { return std::string_view(hash, data_length); }
    const device_id_t &get_device_id() const noexcept { return device_id; }
    std::string serialize() noexcept {
        db::SomeDevice db;
        serialize(db);
        return db.SerializeAsString();
    }

    void serialize(db::SomeDevice &db) const noexcept {
        db.set_name(name);
        db.set_client_name(client_name);
        db.set_client_version(client_version);
        db.set_address(address);
        db.set_last_seen(utils::as_seconds(last_seen));
    }

    std::string_view get_name() const noexcept { return name; }
    std::string_view get_client_name() const noexcept { return client_name; }
    std::string_view get_client_version() const noexcept { return client_version; }
    std::string_view get_address() const noexcept { return address; }
    const pt::ptime &get_last_seen() const noexcept { return last_seen; }
    void set_last_seen(const pt::ptime &value) noexcept { last_seen = value; }

    static const constexpr size_t digest_length = 32;
    static const constexpr size_t data_length = digest_length + 1;

    struct SYNCSPIRIT_API map_t : public generic_map_t<ptr_t, 1> {
        using parent_t = generic_map_t<ptr_t, 1>;
        ptr_t by_sha256(std::string_view value) const noexcept { return this->template get<0>(value); }
    };

    void assign(const db::SomeDevice &db) noexcept {
        name = db.name();
        client_name = db.client_name();
        client_version = db.client_version();
        address = db.address();
        last_seen = pt::from_time_t(db.last_seen());
    }

  private:
    some_device_t(const device_id_t &device_id_, const db::SomeDevice &db) noexcept : device_id{device_id_} {
        auto sha256 = device_id.get_sha256();
        hash[0] = prefix;
        std::copy(sha256.begin(), sha256.end(), hash + 1);
        assign(db);
    }

    std::string name;
    std::string client_name;
    std::string client_version;
    std::string address;
    pt::ptime last_seen;
    device_id_t device_id;

    char hash[data_length];
};

} // namespace syncspirit::model
