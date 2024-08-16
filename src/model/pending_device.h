// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "some_device.hpp"
#include "db/prefix.h"

namespace syncspirit::model {

using pending_device_t = some_device_t<(char)db::prefix::pending_device>;
using pending_devices_map_t = typename pending_device_t::map_t;
using pending_device_ptr_t = typename pending_device_t::ptr_t;

}; // namespace syncspirit::model
