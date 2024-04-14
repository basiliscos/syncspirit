// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)

#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#endif

namespace syncspirit::utils {

struct platform_t {
    SYNCSPIRIT_API static void startup();
    SYNCSPIRIT_API static void shutdown() noexcept;
};

} // namespace syncspirit::utils
