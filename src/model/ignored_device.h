// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "some_device.h"
#include "db/prefix.h"

namespace syncspirit::model {

using ignored_device_t = some_device_t<(char)db::prefix::ignored_device>;
using ignored_devices_map_t = typename ignored_device_t::map_t;
using ignored_device_ptr_t = typename ignored_device_t::ptr_t;

}; // namespace syncspirit::model
