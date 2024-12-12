// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include "../device.h"
#include "bep.pb.h"

namespace syncspirit::model {

SYNCSPIRIT_API void record_update(proto::Vector &v, const device_t &source) noexcept;

} // namespace syncspirit::model
