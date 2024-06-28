// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "misc/arc.hpp"
#include "misc/map.hpp"
#include "misc/error_code.h"

#include "device_id.h"
#include "syncspirit-export.h"
#include "structs.pb.h"

#include <boost/outcome.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace syncspirit::model {

namespace outcome = boost::outcome_v2;
namespace pt = boost::posix_time;

template <char prefix> struct SYNCSPIRIT_API some_device_t final : arc_base_t<some_device_t<prefix>> {

    using ptr_t = intrusive_ptr_t<some_device_t>;

    some_device_t(const some_device_t &other) : label{other.label}, address{other.address}, last_seen{other.last_seen} {
        std::copy(other.hash, other.hash + data_length, hash);
    }

    static outcome::result<ptr_t> create(const device_id_t &id, const db::SomeDevice &db) noexcept {
        return ptr_t(new some_device_t(id, db));
    }

    std::string_view get_key() const noexcept { return std::string_view(hash, data_length); }
    std::string_view get_sha256() const noexcept { return std::string_view(hash + 1, digest_length); }
    std::string serialize() noexcept {
        db::SomeDevice db;
        serialize(db);
        return db.SerializeAsString();
    }

    void serialize(db::SomeDevice db) const noexcept {
        pt::ptime epoch(boost::gregorian::date(1970, 1, 1));
        auto time_diff = last_seen - epoch;
        auto last_seen_time = time_diff.ticks() / time_diff.ticks_per_second();

        db.set_label(label);
        db.set_address(address);
        db.set_last_seen(last_seen_time);
    }

    std::string_view get_label() const noexcept { return label; }
    std::string_view get_address() const noexcept { return address; }
    const pt::ptime &get_last_seen() const noexcept { return last_seen; }
    void set_last_seen(const pt::ptime &value) noexcept { last_seen = value; }

    static const constexpr size_t digest_length = 32;
    static const constexpr size_t data_length = digest_length + 1;

    struct SYNCSPIRIT_API map_t : public generic_map_t<ptr_t, 1> {
        using parent_t = generic_map_t<ptr_t, 1>;
        ptr_t by_sha256(std::string_view value) noexcept { return this->template get<0>(value); }
    };

  private:
    some_device_t(const device_id_t &device_id, const db::SomeDevice &db) noexcept {
        auto sha256 = device_id.get_sha256();
        hash[0] = prefix;
        std::copy(sha256.begin(), sha256.end(), hash + 1);
        label = db.label();
        address = db.address();
        last_seen = pt::from_time_t(db.last_seen());
    }

    std::string label;
    std::string address;
    pt::ptime last_seen;

    some_device_t(std::string_view key) noexcept;
    char hash[data_length];
};

} // namespace syncspirit::model
