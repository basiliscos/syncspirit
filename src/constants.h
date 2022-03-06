// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <cstdint>

namespace syncspirit::constants {
static const constexpr std::uint32_t bep_magic = 0x2EA7D90B;
static const constexpr std::uint32_t rescan_interval = 3600;
extern const char *client_name;
extern const char *issuer_name;
extern const char *protocol_name;
extern const char *client_version;

} // namespace syncspirit::constants
