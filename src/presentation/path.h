// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include <string>
#include <vector>
#include <cstdint>

namespace syncspirit::presentation {

struct SYNCSPIRIT_API path_t {

    struct SYNCSPIRIT_API iterator_t {
        using difference_type = std::ptrdiff_t;
        using element_type = std::string_view;
        using pointer = element_type *;
        using reference = element_type;

        iterator_t() noexcept;
        iterator_t(const path_t *path) noexcept;
        reference operator*() const noexcept;
        iterator_t &operator++() noexcept;
        bool operator==(iterator_t) noexcept;

        std::int32_t position;
        const path_t *path;
    };

    path_t() noexcept = default;
    path_t(std::string_view full_name) noexcept;
    path_t(path_t &&) noexcept = default;

    iterator_t begin() const noexcept;
    iterator_t end() const noexcept;

    std::size_t get_pieces_size() const noexcept;

    std::string_view get_full_name() const noexcept;
    std::string_view get_own_name() const noexcept;
    std::string_view get_parent_name() const noexcept;

  private:
    using pieces_t = std::vector<std::uint32_t>;
    std::string name;
    pieces_t pieces;
};

} // namespace syncspirit::presentation
