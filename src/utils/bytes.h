// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include <span>
#include <vector>

namespace syncspirit::utils {

using bytes_view_t = std::span<const unsigned char>;
using bytes_t = std::vector<unsigned char>;

}

bool operator==(syncspirit::utils::bytes_view_t, syncspirit::utils::bytes_view_t) noexcept;
