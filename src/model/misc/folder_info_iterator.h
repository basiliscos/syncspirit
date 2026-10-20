// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include "arc.hpp"
#include <cstdint>

namespace syncspirit::model {

struct file_info_t;
struct folder_info_t;
struct presence_t;
using folder_info_ptr_t = intrusive_ptr_t<folder_info_t>;

struct SYNCSPIRIT_API folder_info_iterator_t: arc_base_t<folder_info_iterator_t> {
    folder_info_iterator_t(folder_info_ptr_t fi) noexcept;

    file_info_t* next() noexcept;
    void reset(bool full) noexcept;
    void set_min_sequence(std::int64_t value) noexcept;
    bool is_finished() const noexcept;
    inline std::int64_t get_seen_sequnece() const noexcept { return seen_sequence; }

private:
    file_info_t* advance() noexcept;
    std::int64_t seen_sequence{0};
    std::int64_t max_sequence{0};
    std::int64_t min_sequence{0};
    presence_t* current{nullptr};
    folder_info_ptr_t fi;
};

using folder_info_iterator_ptr_t = model::intrusive_ptr_t<folder_info_iterator_t>;


}