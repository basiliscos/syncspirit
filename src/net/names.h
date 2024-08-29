// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"

namespace syncspirit {
namespace net {

struct SYNCSPIRIT_API names {
    static const char *peer_supervisor;
    static const char *coordinator;
    static const char *resolver;
    static const char *http10;
    static const char *http11_gda;
    static const char *http11_relay;
    static const char *hasher_proxy;
    static const char *fs_actor;
    static const char *fs_scheduler;
};

} // namespace net
}; // namespace syncspirit
