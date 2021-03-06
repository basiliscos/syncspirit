// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "map.hpp"

namespace syncspirit::model {

using string_map = syncspirit::model::generic_map_t<std::string, 1>;

template <> inline std::string_view get_index<0>(const std::string &item) noexcept { return item; }

} // namespace syncspirit::model
