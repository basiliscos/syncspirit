// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <string_view>
#include <utility>
#include <optional>

namespace syncspirit::daemon::command {

struct pair_iterator_t {
    using pair_t = std::pair<std::string_view, std::string_view>;

    inline pair_iterator_t(std::string_view in_) noexcept : in(in_) {}

    std::optional<pair_t> next(bool skip_colon = false) noexcept;
    std::string_view in;
};

} // namespace syncspirit::daemon::command
