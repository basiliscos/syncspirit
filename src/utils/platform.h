// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

namespace syncspirit::utils {

struct platform_t {
    static void startup();
    static void shutdhown() noexcept;
};

} // namespace syncspirit::utils
