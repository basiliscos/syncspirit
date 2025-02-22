// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)

#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#endif

namespace syncspirit::utils {

struct SYNCSPIRIT_API platform_t {
    static void startup();
    static void shutdown() noexcept;
    static bool symlinks_supported() noexcept;
};

} // namespace syncspirit::utils
