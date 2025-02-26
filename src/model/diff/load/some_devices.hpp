// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "model/device_id.h"
#include "model/misc/error_code.h"
#include "proto/proto-structs.h"
#include "common.h"

#include <boost/outcome.hpp>

namespace syncspirit::model::diff::load {

namespace outcome = boost::outcome_v2;

struct some_devices_t {
    template <typename T> static outcome::result<void> apply(const container_t items, typename T::map_t &map) noexcept {
        for (auto &pair : items) {
            auto key = pair.key;
            auto sha256 = key.subspan(1, T::digest_length);
            auto device_id_opt = device_id_t::from_sha256(sha256);
            if (!device_id_opt) {
                return make_error_code(error_code_t::malformed_deviceid);
            }
            auto &device_id = *device_id_opt;

            auto opt = db::decode::some_device(pair.value);
            if (!opt) {
                return make_error_code(error_code_t::some_device_deserialization_failure);
            }
            auto option = T::create(device_id, opt.value());
            if (!option) {
                return option.assume_error();
            }
            auto &device = option.assume_value();
            map.put(std::move(device));
        }
        return outcome::success();
    }
};

} // namespace syncspirit::model::diff::load
