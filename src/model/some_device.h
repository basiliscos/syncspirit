// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include <boost/asio.hpp>
#include "misc/augmentation.hpp"
#include "misc/map.hpp"
#include "misc/error_code.h"

#include "device_id.h"
#include "syncspirit-export.h"
#include "proto/proto-fwd.hpp"

#include <boost/outcome.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace syncspirit::model {

namespace outcome = boost::outcome_v2;
namespace pt = boost::posix_time;

struct SYNCSPIRIT_API some_device_base_t {
    static const constexpr size_t digest_length = 32;
    static const constexpr size_t data_length = digest_length + 1;

    some_device_base_t(const some_device_base_t &other) = delete;
    void assign(const db::SomeDevice &db) noexcept;
    utils::bytes_view_t get_key() const noexcept { return utils::bytes_view_t(hash, data_length); }
    const device_id_t &get_device_id() const noexcept { return device_id; }

    std::string_view get_name() const noexcept;
    std::string_view get_client_name() const noexcept;
    std::string_view get_client_version() const noexcept;
    std::string_view get_address() const noexcept;
    const pt::ptime &get_last_seen() const noexcept;
    void set_last_seen(const pt::ptime &value) noexcept;

    utils::bytes_t serialize() noexcept;
    void serialize(db::SomeDevice &db) const noexcept;

  protected:
    some_device_base_t(char prefix, const device_id_t &device_id_, const db::SomeDevice &db) noexcept;

    std::string name;
    std::string client_name;
    std::string client_version;
    std::string address;
    pt::ptime last_seen;
    device_id_t device_id;
    unsigned char hash[data_length];
};

template <char prefix>
struct SYNCSPIRIT_API some_device_t final : some_device_base_t, augmentable_t<some_device_t<prefix>> {
    using ptr_t = intrusive_ptr_t<some_device_t>;

    static outcome::result<ptr_t> create(const device_id_t &id, const db::SomeDevice &db) noexcept {
        return ptr_t(new some_device_t(id, db));
    }

    struct SYNCSPIRIT_API map_t : public generic_map_t<ptr_t, 1> {
        using parent_t = generic_map_t<ptr_t, 1>;
        ptr_t by_sha256(utils::bytes_view_t value) const noexcept { return this->template get<0>(value); }
    };

  private:
    some_device_t(const device_id_t &device_id_, const db::SomeDevice &db) noexcept
        : some_device_base_t(prefix, device_id_, db) {}
};

} // namespace syncspirit::model
