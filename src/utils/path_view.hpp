// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "path.h"
#include "path_decomposer.hpp"
#include "fs/utils.h"
#include <memory>
#include <cstring>

namespace syncspirit::utils {

template <typename Allocator> struct path_view_t final : path_base_t {
    using Traits = std::allocator_traits<Allocator>;
    using AllocatorU32 = Traits::template rebind_alloc<std::uint32_t>;
    using TraitsU32 = std::allocator_traits<AllocatorU32>;
    using T = typename Allocator::value_type;
    using wallocator_t = std::pmr::polymorphic_allocator<wchar_t>;
    using wstring_t = std::basic_string<wchar_t, std::char_traits<wchar_t>, wallocator_t>;

    path_view_t(const Allocator &allocator_) noexcept : allocator{allocator_} {}

    explicit path_view_t(const path_base_t &path, const Allocator &allocator_) noexcept : allocator{allocator_} {
        copy(path);
    }

    explicit path_view_t(const void *data_, std::uint32_t components_, const Allocator &allocator_) noexcept
        : allocator{allocator_} {
        data = data_;
        components = components_;
    }

    template <typename CharT, typename Traits = details::traits::generic<CharT>>
    explicit path_view_t(std::basic_string_view<CharT> normalized, const Allocator &allocator_,
                         const Traits &t) noexcept
        : allocator{allocator_} {
        auto separators = Traits::separators;
        auto decomposed = path_decomposer_t::decompose(normalized, allocator, separators);
        data = decomposed.data;
        components = decomposed.components;
    }

    ~path_view_t() {
        if (data) {
            deallocate();
        }
    }

    void deallocate() {
        auto str_sz = *reinterpret_cast<const std::uint32_t *>(data);
        auto sz = sizeof(std::uint32_t) + components + str_sz + 1;
        auto ptr = const_cast<T *>(reinterpret_cast<const T *>(data));
        Traits::deallocate(allocator, ptr, sz);
        data = nullptr;
        components = 0;
    }

    path_view_t get_parent() const noexcept {
        if (components) {
            assert(data);
            auto str_sz = *reinterpret_cast<const std::uint32_t *>(data);
            auto ptr = reinterpret_cast<const std::uint8_t *>(data) + sizeof(std::uint32_t);
            auto new_str_sz = std::uint32_t{0};
            for (size_t i = 0; i < components; ++i) {
                new_str_sz += ptr[i];
            }
            if (new_str_sz > 1)
                --new_str_sz; // skip trailing '/'
            auto new_sz = sizeof(std::uint32_t) + components - 1 + new_str_sz + 1;
            auto allocator_u32 = AllocatorU32(allocator);
            auto new_ptr = TraitsU32::allocate(allocator_u32, new_sz);

            auto raw_u32 = reinterpret_cast<std::uint32_t *>(new_ptr);
            *raw_u32++ = new_str_sz;
            auto raw_u8_ptr = reinterpret_cast<std::uint8_t *>(raw_u32);
            for (size_t i = 0; i < components - 1; ++i) {
                *raw_u8_ptr++ = ptr[i];
            }
            std::memcpy(raw_u8_ptr, ptr + components, new_str_sz);
            raw_u8_ptr[new_str_sz] = 0;
            return path_view_t(new_ptr, components - 1, allocator);
        }
        return {allocator};
    }

    path_view_t make_temporal() const noexcept {
        if (data) {
            auto u32_ptr = reinterpret_cast<const std::uint32_t *>(data);
            auto str_sz = *u32_ptr++;
            auto tmp_sz = fs::tmp_suffix.size();
            auto new_str_sz = str_sz + tmp_sz;
            auto ptr = reinterpret_cast<const std::uint8_t *>(u32_ptr);
            auto new_sz = sizeof(std::uint32_t) + components + new_str_sz + 1;
            auto allocator_u32 = AllocatorU32(allocator);
            auto new_ptr = TraitsU32::allocate(allocator_u32, new_sz);

            auto new_raw_u32 = reinterpret_cast<std::uint32_t *>(new_ptr);
            *new_raw_u32++ = new_str_sz;

            auto new_raw_u8_ptr = reinterpret_cast<std::uint8_t *>(new_raw_u32);
            for (size_t i = 0; i < components; ++i) {
                *new_raw_u8_ptr++ = *ptr++;
            }
            std::memcpy(new_raw_u8_ptr, ptr, str_sz);
            new_raw_u8_ptr += str_sz;
            std::memcpy(new_raw_u8_ptr, fs::tmp_suffix.data(), tmp_sz);
            new_raw_u8_ptr += tmp_sz;
            *new_raw_u8_ptr = 0;
            return path_view_t(new_ptr, components, allocator);
        }
        return {allocator};
    }

    const Allocator &get_allocator() const noexcept { return allocator; }

    path_t detach() const noexcept {
        if (data) {
            return path_t(data, components);
        }
        return {};
    }

    path_view_t clone() const noexcept { return path_view_t(*this, allocator); }

    path_view_t &operator=(const path_view_t &path) noexcept {
        if (data) {
            deallocate();
        }
        copy(path);
        return *this;
    }

    wstring_t get_full_wname(bool native_separator = false) const noexcept {
        using namespace boost::nowide;
        using namespace boost::nowide::utf;

        using traits_in_t = utf_traits<char>;
        using traits_out_t = utf_traits<wchar_t>;

        auto w_allocator = wallocator_t(allocator);
        auto r = wstring_t(w_allocator);
        if (data) {
            auto sz = *reinterpret_cast<const std::uint32_t *>(data);
            auto begin = reinterpret_cast<const char *>(data) + sizeof(std::uint32_t) + components;
            auto end = begin + sz;
            auto ptr = begin;

            auto w_sz = std::size_t{0};
            while (ptr != end) {
                traits_in_t::decode(ptr, end);
                ++w_sz;
            }

            r.resize(w_sz);
            auto out = r.data();
            ptr = begin;

            while (ptr != end) {
                auto symbol = traits_in_t::decode(ptr, end);
                if (native_separator && symbol == L'/') {
                    *out++ = details::traits::native<wchar_t>::separators[0];
                } else {
                    *out++ = symbol;
                }
            }
        }
        return r;
    }

  private:
    void copy(const path_base_t &path) {
        if (auto d = path.get_data(); d) {
            auto str_sz = *reinterpret_cast<const std::uint32_t *>(d);
            auto c = path.get_components();
            auto sz = sizeof(std::uint32_t) + c + str_sz + 1;
            auto allocator_u32 = AllocatorU32(allocator);
            data = TraitsU32::allocate(allocator_u32, sz);
            memcpy(const_cast<void *>(data), d, sz);
            components = c;
        }
    }

    mutable Allocator allocator;
};

template <typename Allocator>
path_view_t<Allocator> join(const path_base_t &parent, const path_base_t &child, const Allocator &allocator_) noexcept {
    using Traits = std::allocator_traits<Allocator>;
    using AllocatorU32 = Traits::template rebind_alloc<std::uint32_t>;
    using TraitsU32 = std::allocator_traits<AllocatorU32>;
    if (parent.empty()) {
        return path_view_t(child, allocator_);
    }
    if (child.empty()) {
        return path_view_t(parent, allocator_);
    }
    if (child.is_absolute()) {
        return path_view_t(child, allocator_);
    }
    auto allocator = AllocatorU32(allocator_);
    auto ptr_1 = reinterpret_cast<const std::uint8_t *>(parent.get_data());
    auto ptr_2 = reinterpret_cast<const std::uint8_t *>(child.get_data());
    auto str_sz_1 = *reinterpret_cast<const std::uint32_t *>(ptr_1);
    auto str_sz_2 = *reinterpret_cast<const std::uint32_t *>(ptr_2);

    ptr_1 += sizeof(std::uint32_t);
    ptr_2 += sizeof(std::uint32_t);

    auto new_str_sz = str_sz_1 + str_sz_2 + 1; // "/" between

    auto new_components = parent.get_components() + child.get_components() + 1; // "/"
    auto new_sz = sizeof(std::uint32_t) + new_components + new_str_sz + 1;
    auto new_ptr = TraitsU32::allocate(allocator, new_sz);
    auto new_u32_ptr = reinterpret_cast<std::uint32_t *>(new_ptr);
    *new_u32_ptr++ = new_str_sz;

    auto new_u8_ptr = reinterpret_cast<std::uint8_t *>(new_u32_ptr);
    auto traier_1_sz = std::uint32_t{0};
    for (std::uint32_t i = 0; i < parent.get_components(); ++i) {
        auto piece_sz = *ptr_1++;
        traier_1_sz += piece_sz;
        *new_u8_ptr++ = piece_sz;
    }
    auto last_piece_1_sz = str_sz_1 - traier_1_sz;
    *new_u8_ptr++ = last_piece_1_sz + 1;
    for (std::uint32_t i = 0; i < child.get_components(); ++i) {
        *new_u8_ptr++ = *ptr_2++;
    }

    std::memcpy(new_u8_ptr, ptr_1, str_sz_1);
    new_u8_ptr += str_sz_1;
    *new_u8_ptr++ = '/';

    std::memcpy(new_u8_ptr, ptr_2, str_sz_2);
    new_u8_ptr += str_sz_2;
    *new_u8_ptr++ = 0;
    return path_view_t(new_ptr, new_components, allocator_);
}

template <typename Allocator>
auto operator/(const path_view_t<Allocator> &parent, const path_view_t<Allocator> &child) noexcept
    -> path_view_t<Allocator> {
    return join(parent, child, parent.get_allocator());
}

template <typename Allocator>
auto operator/(const path_view_t<Allocator> &parent, const path_base_t &child) noexcept -> path_view_t<Allocator> {
    return join(parent, child, parent.get_allocator());
}

template <typename Allocator>
auto operator/(const path_base_t &parent, const path_view_t<Allocator> &child) noexcept -> path_view_t<Allocator> {
    return join(parent, child, child.get_allocator());
}

template <typename Allocator> auto path_base_t::get_view(const Allocator &a) const noexcept -> path_view_t<Allocator> {
    return path_view_t<Allocator>(*this, a);
};

template <typename T, typename Allocator, typename TP = std::remove_reference_t<std::remove_cv_t<T>>,
          typename CharT = typename std::char_traits<typename TP::value_type>::char_type,
          typename = std::enable_if_t<std::is_convertible_v<T &&, std::basic_string_view<CharT>>>>
auto make_generic_view(T &&normalized_path, const Allocator &a) noexcept -> path_view_t<Allocator> {
    using str_t = std::basic_string_view<CharT>;
    using Traits = details::traits::generic<CharT>;
    return path_view_t<Allocator>(str_t(normalized_path), a, Traits{});
};

template <typename Allocator>
auto make_generic_view(const char *normalized_path, const Allocator &a) noexcept -> path_view_t<Allocator> {
    return make_generic_view(std::string_view(normalized_path), a);
};

template <typename Allocator>
auto make_generic_view(const wchar_t *normalized_path, const Allocator &a) noexcept -> path_view_t<Allocator> {
    return make_generic_view(std::wstring_view(normalized_path), a);
};

template <typename T, typename Allocator, typename TP = std::remove_reference_t<std::remove_cv_t<T>>,
          typename CharT = typename std::char_traits<typename TP::value_type>::char_type,
          typename = std::enable_if_t<std::is_convertible_v<T &&, std::basic_string_view<CharT>>>>
auto make_native_view(T &&normalized_path, const Allocator &a) noexcept -> path_view_t<Allocator> {
    using str_t = std::basic_string_view<CharT>;
    using Traits = details::traits::native<CharT>;
    return path_view_t<Allocator>(str_t(normalized_path), a, Traits{});
};

template <typename Allocator>
auto make_native_view(const char *normalized_path, const Allocator &a) noexcept -> path_view_t<Allocator> {
    return make_native_view(std::string_view(normalized_path), a);
};

template <typename Allocator>
auto make_native_view(const wchar_t *normalized_path, const Allocator &a) noexcept -> path_view_t<Allocator> {
    return make_native_view(std::wstring_view(normalized_path), a);
};

template <typename Allocator> auto make_empty_view(const Allocator &a) noexcept -> path_view_t<Allocator> {
    return make_generic_view(std::string_view(), a);
};

using allocator_t = std::pmr::polymorphic_allocator<char>;
using poly_path_view_t = path_view_t<allocator_t>;

} // namespace syncspirit::utils
