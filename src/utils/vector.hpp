// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include <boost/container/options.hpp>
#include <boost/container/vector.hpp>

#include <cstdint>

namespace syncspirit::utils {

namespace _bc = boost::container;

template <typename Sizer>
using vector_opts_t = _bc::vector_options<_bc::stored_size<Sizer>, _bc::growth_factor<_bc::growth_factor_50>>::type;

template <typename T, typename Sizer = std::uint32_t>
using vector_t = typename _bc::vector<T, void, vector_opts_t<Sizer>>;

} // namespace syncspirit::utils
