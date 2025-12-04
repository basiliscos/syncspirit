// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"

namespace syncspirit::net {

struct SYNCSPIRIT_API names {
    static const char *peer_supervisor;
    static const char *coordinator;
    static const char *resolver;
    static const char *http10;
    static const char *http11_gda;
    static const char *http11_relay;
    static const char *db;
    static const char *fs_actor;
    static const char *fs_scanner;
    static const char *fs_scheduler;
};

} // namespace syncspirit::net
