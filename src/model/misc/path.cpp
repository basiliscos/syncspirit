// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "path.h"
#include "path_decomposer.hpp"
#include "fs/utils.h"
#include <cstring>
#include <cassert>

namespace bfs = std::filesystem;

using namespace syncspirit::model;

using I = path_t::iterator_t;

path_t path_t::make_native(std::string_view name) noexcept {
    auto buffer = std::array<std::byte, 1024 * 5>{};
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto separators = details::traits::native<char>::separators;
    auto decomposed = path_decomposer_t::decompose<true>(name, allocator, separators);
    return path_t(decomposed.data, decomposed.components);
}

path_t path_t::make_native(std::wstring_view name) noexcept {
    auto buffer = std::array<std::byte, 1024 * 5>{};
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto separators = details::traits::native<wchar_t>::separators;
    auto decomposed = path_decomposer_t::decompose<true>(name, allocator, separators);
    return path_t(decomposed.data, decomposed.components);
}

path_t path_t::make_generic(std::string_view name) noexcept {
    auto buffer = std::array<std::byte, 1024 * 5>{};
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto separators = details::traits::generic<char>::separators;
    auto decomposed = path_decomposer_t::decompose<true>(name, allocator, separators);
    return path_t(decomposed.data, decomposed.components);
}

path_t path_t::make_generic(std::wstring_view name) noexcept {
    auto buffer = std::array<std::byte, 1024 * 5>{};
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<wchar_t>(&pool);
    auto separators = details::traits::generic<wchar_t>::separators;
    auto decomposed = path_decomposer_t::decompose<true>(name, allocator, separators);
    return path_t(decomposed.data, decomposed.components);
}

path_t::path_t(path_t &&other) noexcept {
    std::swap(data, other.data);
    std::swap(components, other.components);
}

#if 0
path_t::path_t(std::string_view full_name) noexcept {
    auto buffer = std::array<std::byte, 1024 * 5>{};
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto decomposed = path_decomposer_t::decompose<true>(full_name, allocator);
    data = decomposed.data;
    components = decomposed.components;
}
#endif

path_t::path_t(const void *data_, std::uint32_t components_) noexcept {
    data = data_;
    components = components_;
}

path_t::~path_t() {
    if (data) {
        ::operator delete(const_cast<void *>(data), path_alignment);
    }
}

bool path_base_t::operator==(const path_base_t &other) const noexcept {
    if (components == other.components) {
        auto ptr_1 = reinterpret_cast<const uint8_t *>(data);
        auto ptr_2 = reinterpret_cast<const uint8_t *>(other.data);
        auto sz_1 = *reinterpret_cast<const uint32_t *>(ptr_1++);
        auto sz_2 = *reinterpret_cast<const uint32_t *>(ptr_2++);
        if (sz_1 == sz_2) {
            auto sz = sz_1 + components;
            return std::memcmp(ptr_1, ptr_2, sz) == 0;
        }
    }
    return false;
}

path_t &path_t::operator=(path_t &&other) noexcept {
    std::swap(data, other.data);
    std::swap(components, other.components);
    return *this;
}

bool path_base_t::empty() const noexcept { return data == nullptr; }

std::size_t path_base_t::get_pieces_size() const noexcept {
    if (data) {
        // return pieces.size() + (!name.empty() ? 1 : 0);
        return components + 1;
    }
    return 0;
}

bool path_base_t::contains(const path_base_t &other) const noexcept {
    if (data) {
        if (data) {
            auto sz_1 = *reinterpret_cast<const std::uint32_t *>(data);
            auto sz_2 = *reinterpret_cast<const std::uint32_t *>(other.data);
            if (sz_1 <= sz_2) {
                auto ptr_1 = reinterpret_cast<const uint8_t *>(data) + sizeof(std::uint32_t) + components;
                auto ptr_2 = reinterpret_cast<const uint8_t *>(other.data) + sizeof(std::uint32_t) + other.components;
                for (std::uint32_t i = 0; i < sz_1; ++i) {
                    if (*ptr_1++ != *ptr_2++) {
                        return false;
                    }
                }
                return true;
            }
        } else {
            return true;
        }
    }
    return false;
}

bool path_base_t::is_temporal() const noexcept {
    if (data) {
        auto it = iterator_t(this, components);
        auto filename = *it;
        auto tmp_sz = fs::tmp_suffix.size();
        if (filename.size() >= tmp_sz) {
            auto ptr_2 = fs::tmp_suffix.data();
            auto end_2 = ptr_2 + tmp_sz;
            auto ptr_1 = filename.data() + filename.size() - tmp_sz;
            for (; ptr_2 != end_2; ptr_1++, ptr_2++) {
                if (*ptr_2 != *ptr_1) {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

bool path_base_t::is_absolute() const noexcept {
    if (data) {
        auto sz = *reinterpret_cast<const std::uint32_t *>(data);
        auto ptr = reinterpret_cast<const char *>(data) + sizeof(std::uint32_t) + components;
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
        if (sz >= 3) {
            auto first_letter = (ptr[0] >= 'a' && ptr[0] <= 'z') || (ptr[0] >= 'A' && ptr[0] <= 'Z');
            if (first_letter && ptr[1] == ':' && ptr[2] == '/') {
                return true;
            }
        }
#else
        if (sz) {
            if (*ptr == '/') {
                return true;
            }
        }

#endif
    }
    return false;
}

std::string_view path_base_t::get_full_name() const noexcept {
    if (data) {
        auto sz = *reinterpret_cast<const std::uint32_t *>(data);
        auto ptr = reinterpret_cast<const char *>(data) + sizeof(std::uint32_t) + components;
        return std::string_view(ptr, ptr + sz);
    }
    return {};
}

std::string_view path_base_t::get_filename() const noexcept {
    if (data) {
        auto it = iterator_t(this, components);
        return *it;
    }
    return {};
}

std::string_view path_base_t::get_parent_name() const noexcept {
    if (components >= 1) {
        auto first = (*iterator_t(this));
        auto last = (*iterator_t(this, components - 1));
        return std::string_view(first.begin(), last.end());
    }
    return {};
}

auto path_base_t::begin() const noexcept -> iterator_t { return iterator_t(this); }

auto path_base_t::end() const noexcept -> iterator_t { return iterator_t(); }

I::iterator_t() noexcept : component{-1}, path{nullptr} {}

I::iterator_t(const path_base_t *path_, std::int32_t component_) noexcept {
    if (path_->data) {
        assert(component_ >= 0);
        assert(component_ <= path_->components);
        component = component_;
        path = path_;
    } else {
        component = -1;
        path = nullptr;
    }
}

I &I::operator++() noexcept {
    assert(path);
    assert(component >= 0);
    assert(static_cast<std::uint32_t>(component) <= path->components);

    ++component;
    if (static_cast<std::uint32_t>(component) > path->components) {
        path = nullptr;
        component = -1;
    }
    return *this;
}

auto I::operator*() const noexcept -> reference {
    if (path) {
        assert(static_cast<std::uint32_t>(component) <= path->components);
        auto ptr = reinterpret_cast<const std::uint32_t *>(path->data);
        auto sz = *ptr++;
        auto piece_ptr = reinterpret_cast<const std::uint8_t *>(ptr);
        auto begin_ptr = reinterpret_cast<const char *>(piece_ptr) + path->components;
        auto data_ptr = begin_ptr;
        if (!component && sz == 1 && *begin_ptr == '/') {
            ++data_ptr;
        }
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(component); ++i) {
            data_ptr += *piece_ptr++;
        }
        auto end_ptr = component < path->components ? (data_ptr + *piece_ptr - 1) : begin_ptr + sz;
        return std::string_view(data_ptr, end_ptr);
    }
    return {};
}

bool I::operator==(iterator_t other) noexcept { return (path == other.path) && component == other.component; }
