// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "uuid.h"
#include <cassert>

namespace syncspirit::model {

void assign(bu::uuid &uuid, std::string_view source) noexcept {
    assert(source.size() == uuid.size());
    auto data = (const uint8_t *)source.data();
    std::copy(data, data + source.size(), &uuid.data[0]);
}

} // namespace syncspirit::model
