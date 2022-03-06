// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "uuid.h"
#include <cassert>

namespace syncspirit::model {

void assign(uuid_t &uuid, std::string_view source) noexcept {
    assert(source.size() == uuid.size());
    auto data = (const uint8_t *)source.data();
    std::copy(data, data + source.size(), uuid.data);
}

} // namespace syncspirit::model
