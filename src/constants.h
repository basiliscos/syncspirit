// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <cstdint>
#include "syncspirit-export.h"

namespace syncspirit::constants {
static const constexpr std::uint32_t bep_magic = 0x2EA7D90B;
static const constexpr std::uint32_t rescan_interval = 3600;
SYNCSPIRIT_API extern const char *client_name;
SYNCSPIRIT_API extern const char *issuer_name;
SYNCSPIRIT_API extern const char *protocol_name;
SYNCSPIRIT_API extern const char *relay_protocol_name;

} // namespace syncspirit::constants
