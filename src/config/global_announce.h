// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once
#include <cstdint>
#include <string>
#include "utils/uri.h"

namespace syncspirit::config {

struct global_announce_config_t {
    bool enabled;
    bool debug;
    utils::uri_ptr_t announce_url;
    utils::uri_ptr_t lookup_url;
    std::string cert_file;
    std::string key_file;
    std::uint32_t rx_buff_size;
    std::uint32_t timeout;
    std::uint32_t reannounce_after = 600;
};

} // namespace syncspirit::config
