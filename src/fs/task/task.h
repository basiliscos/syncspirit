// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include "fs/file.h"
#include "fs/execution_context.h"
#include "hasher/messages.h"
#include <cstdint>

namespace syncspirit::fs {

namespace bfs = std::filesystem;
namespace sys = boost::system;
namespace r = rotor;

struct fs_slave_t;
struct execution_context_t;

} // namespace syncspirit::fs
