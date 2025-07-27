// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include <filesystem>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)

#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#endif

namespace syncspirit::utils {

namespace bfs = std::filesystem;

struct SYNCSPIRIT_API platform_t {
    static bool startup();
    static void shutdown() noexcept;
    static bool symlinks_supported() noexcept;
    static bool path_supported(const bfs::path &) noexcept;
    static bool permissions_supported(const bfs::path &) noexcept;
};

} // namespace syncspirit::utils
