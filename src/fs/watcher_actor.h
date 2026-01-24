// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "platform/watcher.h"

namespace syncspirit::fs {

namespace r = rotor;
namespace bfs = std::filesystem;
namespace sys = boost::system;
using watch_actor_t = platform::watcher_t;

} // namespace syncspirit::fs
