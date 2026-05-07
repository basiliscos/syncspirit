// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include "arc.hpp"
#include <cstdint>
#include <string_view>

namespace syncspirit::model {

struct SYNCSPIRIT_API path_t : arc_base_t<path_t> {
    struct SYNCSPIRIT_API iterator_t {
        using difference_type = std::ptrdiff_t;
        using element_type = std::string_view;
        using pointer = element_type *;
        using reference = element_type;

        iterator_t() noexcept;
        iterator_t(const path_t *path, std::int32_t component = 0) noexcept;
        reference operator*() const noexcept;
        iterator_t &operator++() noexcept;
        bool operator==(iterator_t) noexcept;

        std::int32_t component;
        const path_t *path;
    };

    path_t(std::string_view full_name) noexcept;
    path_t() noexcept = default;
    path_t(path_t &&) noexcept;
    path_t(path_t &) noexcept = delete;
    virtual ~path_t();

    iterator_t begin() const noexcept;
    iterator_t end() const noexcept;

    std::size_t get_pieces_size() const noexcept;

    bool empty() const noexcept;
    bool operator==(const path_t &other) const noexcept;

    std::string_view get_full_name() const noexcept;
    std::string_view get_own_name() const noexcept;
    std::string_view get_parent_name() const noexcept;
    bool contains(const path_t &other) const noexcept;

  protected:
    const void *data = nullptr;
    std::uint32_t components{0};
};

using path_ptr_t = intrusive_ptr_t<path_t>;

} // namespace syncspirit::model
