// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include "model/misc/arc.hpp"
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <new>

namespace syncspirit::utils {

template <typename Allocator> struct path_view_t;

struct SYNCSPIRIT_API path_base_t {
    static constexpr auto path_alignment = std::align_val_t(sizeof(std::uint32_t));

    struct SYNCSPIRIT_API iterator_t {
        using difference_type = std::ptrdiff_t;
        using element_type = std::string_view;
        using pointer = element_type *;
        using reference = element_type;

        iterator_t() noexcept;
        iterator_t(const path_base_t *path, std::int32_t component = 0) noexcept;
        reference operator*() const noexcept;
        iterator_t &operator++() noexcept;
        bool operator==(iterator_t) noexcept;

        const path_base_t *path;
        std::int32_t component;
    };

    iterator_t begin() const noexcept;
    iterator_t end() const noexcept;

    virtual ~path_base_t() = default;

    std::size_t get_pieces_size() const noexcept;

    bool empty() const noexcept;
    bool operator==(const path_base_t &other) const noexcept;

    std::string_view get_full_name() const noexcept;
    std::string_view get_filename() const noexcept;
    std::string_view get_extension() const noexcept;
    std::string_view get_parent_name() const noexcept;
    bool contains(const path_base_t &other) const noexcept;
    bool is_temporal() const noexcept;
    bool is_absolute() const noexcept;

    bool are_permissions_supported() const noexcept;

    template <typename Allocator> auto get_view(const Allocator &a) const noexcept -> path_view_t<Allocator>;

    inline const void *get_data() const noexcept { return data; }
    std::uint32_t get_components() const noexcept { return components; }

  protected:
    const void *data = nullptr;
    std::uint32_t components{0};
};

struct SYNCSPIRIT_API path_t : path_base_t, model::arc_base_t<path_t> {
    path_t() noexcept = default;
    path_t(path_t &&) noexcept;
    path_t(path_t &) noexcept = delete;
    ~path_t();

    static path_t make_native(std::string_view) noexcept;
    static path_t make_native(std::wstring_view) noexcept;
    static path_t make_generic(std::string_view) noexcept;
    static path_t make_generic(std::wstring_view) noexcept;

    path_base_t get_parent_view() const noexcept;
    path_t clone() const noexcept;
    path_t &operator=(path_t &&other) noexcept;

  protected:
    explicit path_t(const void *data, std::uint32_t components) noexcept;

    template <typename Allocator> friend struct path_view_t;
};

bool operator<(const path_base_t &parent, const path_base_t &child) noexcept;

using path_ptr_t = model::intrusive_ptr_t<path_t>;

using allocator_t = std::pmr::polymorphic_allocator<char>;
using poly_path_view_t = path_view_t<allocator_t>;

struct path_eq_t {
    using is_transparent = void;

    bool operator()(const path_t &lhs, path_t &rhs) const;
    bool operator()(const path_t &lhs, std::string_view rhs) const;
    bool operator()(const std::string_view lhs, path_t &rhs) const;
    bool operator()(const std::string_view lhs, const std::string_view rhs) const;

    template <typename T>
        requires std::is_constructible_v<std::string_view, const T &>
    bool operator()(const T &lhs, path_t &rhs) const {
        return (*this)(std::string_view(lhs), rhs);
    }

    template <typename T>
        requires std::is_constructible_v<std::string_view, const T &>
    bool operator()(path_t &lhs, const T &rhs) const {
        return (*this)(lhs, std::string_view(rhs));
    }

    template <typename U, typename V>
        requires std::is_constructible_v<std::string_view, const U &> &&
                 std::is_constructible_v<std::string_view, const V &>
    bool operator()(const U &lhs, const V &rhs) const {
        return (*this)(std::string_view(lhs), std::string_view(rhs));
    }
};

struct path_hash_t {
    using is_transparent = void;

    size_t operator()(const path_t &item) const noexcept;
    size_t operator()(std::string_view item) const noexcept;

    template <typename T>
        requires std::is_constructible_v<std::string_view, const T &>
    auto operator()(const T &item) const {
        return (*this)(std::string_view(item));
    }
};

} // namespace syncspirit::utils

namespace std {
template <> struct hash<syncspirit::utils::path_t> : syncspirit::utils::path_hash_t {};
} // namespace std
