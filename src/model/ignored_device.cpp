// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "ignored_device.h"

namespace syncspirit::model {

template <> SYNCSPIRIT_API std::string_view get_index<0>(const ignored_device_ptr_t &item) noexcept {
    return item->get_device_id().get_sha256();
}

} // namespace syncspirit::model
