// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <string_view>
#include <boost/uuid/uuid.hpp>
#include "syncspirit-export.h"

namespace syncspirit::model {

static const constexpr size_t uuid_length = 16;
using uuid_t = boost::uuids::uuid;

SYNCSPIRIT_API void assign(uuid_t &, std::string_view source) noexcept;

} // namespace syncspirit::model
