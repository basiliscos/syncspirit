// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"

namespace syncspirit::utils {

struct platform_t {
    SYNCSPIRIT_API static void startup();
    SYNCSPIRIT_API static void shutdhown() noexcept;
};

} // namespace syncspirit::utils
