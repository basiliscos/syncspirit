// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#pragma once
#include <cstdint>

namespace syncspirit::config {

struct fs_config_t {
    std::uint32_t temporally_timeout;
    std::uint32_t poll_timeout;
    std::uint32_t retension_timeout;
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    std::uint32_t win32_watcher_buff = 1024 * 1024;
#endif
};

} // namespace syncspirit::config
