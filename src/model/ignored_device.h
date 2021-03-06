// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "misc/arc.hpp"
#include "misc/map.hpp"
#include "device_id.h"
#include "syncspirit-export.h"
#include <boost/outcome.hpp>

namespace syncspirit::model {

namespace outcome = boost::outcome_v2;

struct ignored_device_t;

using ignored_device_ptr_t = intrusive_ptr_t<ignored_device_t>;

struct SYNCSPIRIT_API ignored_device_t final : arc_base_t<ignored_device_t> {

    static outcome::result<ignored_device_ptr_t> create(const device_id_t &) noexcept;
    static outcome::result<ignored_device_ptr_t> create(std::string_view key, std::string_view data) noexcept;

    std::string_view get_key() const noexcept;
    std::string_view get_sha256() const noexcept;
    std::string serialize() noexcept;

    static const constexpr size_t digest_length = 32;
    static const constexpr size_t data_length = digest_length + 1;

  private:
    ignored_device_t(const device_id_t &) noexcept;
    ignored_device_t(std::string_view key) noexcept;
    char hash[data_length];
};

using ignored_devices_map_t = generic_map_t<ignored_device_ptr_t, 1>;

} // namespace syncspirit::model
