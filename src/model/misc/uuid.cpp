// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "uuid.h"
#include <cassert>

namespace syncspirit::model {

void assign(bu::uuid &uuid, utils::bytes_view_t source) noexcept {
    assert(source.size() == uuid.size());
    std::copy(source.begin(), source.end(), &uuid.data[0]);
}

} // namespace syncspirit::model
