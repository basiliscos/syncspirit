// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include <utils/bytes.h>
#include <boost/uuid/uuid.hpp>
#include "syncspirit-export.h"

namespace syncspirit::model {

namespace bu = boost::uuids;

static const constexpr size_t uuid_length = 16;

SYNCSPIRIT_API void assign(bu::uuid &, utils::bytes_view_t source) noexcept;

} // namespace syncspirit::model
