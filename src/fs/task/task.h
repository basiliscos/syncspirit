// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include "fs/file.h"
#include "hasher/messages.h"
#include <cstdint>

namespace syncspirit {

namespace hasher {

struct hasher_plugin_t;

}

namespace fs {

namespace bfs = std::filesystem;
namespace sys = boost::system;
namespace r = rotor;

struct fs_slave_t;

} // namespace fs

} // namespace syncspirit
