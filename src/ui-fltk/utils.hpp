// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include <string_view>
#include <algorithm>
#include <cstdint>

namespace syncspirit::fltk {

std::string get_file_size(std::int64_t value);

template <typename NameProvider>
int bisect(std::string_view new_label, int start_index, int end_index, int children_count,
           NameProvider &&name_provider) {
    end_index = std::min(children_count - 1, end_index);
    if (end_index < 0) {
        return 0;
    }
    if (start_index > end_index) {
        return start_index;
    }

    auto right = name_provider(end_index);
    if (new_label > right) {
        return end_index + 1;
    } else if (new_label >= right) {
        return end_index;
    }
    auto left = name_provider(start_index);
    if (left >= new_label) {
        return start_index;
    }

    // int pos = start_index;
    while (start_index <= end_index) {
        auto left = name_provider(start_index);
        if (left >= new_label) {
            return start_index;
        }
        auto right = name_provider(end_index);
        if (right <= new_label) {
            return end_index;
        }

        auto mid_index = (start_index + end_index) / 2;
        auto mid = name_provider(mid_index);
        if (mid > new_label) {
            end_index = mid_index;
        } else {
            if (mid_index == start_index) {
                return start_index + 1;
            }
            start_index = mid_index;
        }
    }
    return start_index;
}

} // namespace syncspirit::fltk
