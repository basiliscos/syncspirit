// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include "../device.h"
#include "bep.pb.h"

namespace syncspirit::model {

enum class version_relation_t { identity, older, newer, conflict };

SYNCSPIRIT_API version_relation_t compare(const proto::Vector &lhs, const proto::Vector &rhs) noexcept;
SYNCSPIRIT_API void increase(proto::Vector &v, const device_t& source) noexcept;

} // namespace syncspirit::model
