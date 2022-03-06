// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once
#include <cstdint>

namespace syncspirit::config {

struct fs_config_t {
    std::uint32_t temporally_timeout;
    std::uint32_t mru_size;
};

} // namespace syncspirit::config
