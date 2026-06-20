// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

namespace syncspirit::proto {

// clang-format off

enum class FileInfoType {
    FILE              = 0,
    DIRECTORY         = 1,
    SYMLINK_FILE      = 2,
    SYMLINK_DIRECTORY = 3,
    SYMLINK           = 4,
};

// clang-format on

} // namespace syncspirit::proto
