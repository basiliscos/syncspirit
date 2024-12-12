// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "version_utils.h"
#include <algorithm>

namespace syncspirit::model {

void record_update(proto::Vector &v, const device_t &source) noexcept {
    bool append = true;
    auto sz = v.counters_size();
    auto device_short = source.as_uint();
    auto value = uint64_t{0};
    auto target_counter = (proto::Counter *)(nullptr);
    for (int i = 0; i < sz; ++i) {
        auto counter = v.mutable_counters(i);
        value = std::max(value, counter->value() + 1);
        if (counter->id() == device_short) {
            target_counter = counter;
        }
    }
    if (!target_counter) {
        target_counter = v.add_counters();
        target_counter->set_id(device_short);
    }
    target_counter->set_value(value);
}

} // namespace syncspirit::model
