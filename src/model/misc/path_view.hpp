#pragma once

#include "path.h"
#include "path_decomposer.hpp"
#include "fs/utils.h"
#include <memory>
#include <cstring>

namespace syncspirit::model {

template <typename Allocator> struct path_view_t final : path_base_t {
    using Traits = std::allocator_traits<Allocator>;
    using T = typename Allocator::value_type;
    using wallocator_t = std::pmr::polymorphic_allocator<wchar_t>;
    using wstring_t = std::basic_string<wchar_t, std::char_traits<wchar_t>, wallocator_t>;

    path_view_t() noexcept = default;

    explicit path_view_t(const path_base_t &path, const Allocator &allocator_) noexcept : allocator{allocator_} {
        if (auto d = path.get_data(); d) {
            auto str_sz = *reinterpret_cast<const std::uint32_t *>(d);
            auto c = path.get_components();
            auto sz = sizeof(std::uint32_t) + c + str_sz + 1;
            data = Traits::allocate(allocator, sz);
            memcpy(const_cast<void *>(data), d, sz);
            components = c;
        }
    }

    explicit path_view_t(const void *data_, std::uint32_t components_, const Allocator &allocator_) noexcept
        : allocator{allocator_} {
        data = data_;
        components = components_;
    }

    explicit path_view_t(std::string_view normalized, const Allocator &allocator_) noexcept : allocator{allocator_} {
        auto decomposed = path_decomposer_t::decompose<false>(normalized, allocator);
        data = decomposed.data;
        components = decomposed.components;
    }

    ~path_view_t() {
        if (data) {
            auto str_sz = *reinterpret_cast<const std::uint32_t *>(data);
            auto sz = sizeof(std::uint32_t) + components + str_sz + 1;
            auto ptr = const_cast<T *>(reinterpret_cast<const T *>(data));
            Traits::deallocate(allocator, ptr, sz);
        }
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
            auto new_ptr = Traits::allocate(allocator, new_sz);

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
        return {};
    }

    path_view_t make_temporal() const noexcept {
        if (data) {
            auto u32_ptr = reinterpret_cast<const std::uint32_t *>(data);
            auto str_sz = *u32_ptr++;
            auto tmp_sz = fs::tmp_suffix.size();
            auto new_str_sz = str_sz + tmp_sz;
            auto ptr = reinterpret_cast<const std::uint8_t *>(u32_ptr);
            auto new_sz = sizeof(std::uint32_t) + components + new_str_sz + 1;
            auto new_ptr = Traits::allocate(allocator, new_sz);

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
        return {};
    }

    const Allocator &get_allocator() const noexcept { return allocator; }

    path_t detach() const noexcept {
        if (data) {
            auto str_sz = *reinterpret_cast<const std::uint32_t *>(data);
            auto sz = sizeof(std::uint32_t) + components + str_sz + 1;
            auto new_ptr = static_cast<uint8_t *>(::operator new(sz, path_alignment));
            std::memcpy(new_ptr, data, sz);
            return path_t(new_ptr, components);
        }
        return {};
    }

    wstring_t get_full_wname() const noexcept {
        using namespace boost::nowide;
        using namespace boost::nowide::utf;

        using traits_in_t = utf_traits<char>;
        using traits_out_t = utf_traits<wchar_t>;

        auto w_allocator = wallocator_t(allocator);
        auto r = wstring_t(w_allocator);
        if (data) {
            auto sz = *reinterpret_cast<const std::uint32_t *>(data);
            auto begin = reinterpret_cast<const char *>(data) + sizeof(std::uint32_t);
            auto end = begin + sz;
            auto ptr = reinterpret_cast<const char *>(data) + sizeof(std::uint32_t);

            auto w_sz = std::size_t{0};
            while (ptr != end) {
                traits_in_t::decode(ptr, end);
                ++w_sz;
            }

            r.resize(w_sz);
            auto out = r.data();
            ptr = begin;

            while (ptr != end) {
                *out++ = traits_in_t::decode(ptr, end);
            }
        }
        return r;
    }

  private:
    mutable Allocator allocator;
};

template <typename Allocator>
auto operator/(const path_view_t<Allocator> &parent, const path_view_t<Allocator> &child) noexcept
    -> path_view_t<Allocator> {
    using Traits = std::allocator_traits<Allocator>;
    if (parent.empty()) {
        return child;
    }
    if (child.empty()) {
        return parent;
    }
    if (child.is_absolute()) {
        return child;
    }
    auto allocator = parent.get_allocator();
    auto ptr_1 = reinterpret_cast<const std::uint8_t *>(parent.get_data());
    auto ptr_2 = reinterpret_cast<const std::uint8_t *>(child.get_data());
    auto str_sz_1 = *reinterpret_cast<const std::uint32_t *>(ptr_1);
    auto str_sz_2 = *reinterpret_cast<const std::uint32_t *>(ptr_2);

    ptr_1 += sizeof(std::uint32_t);
    ptr_2 += sizeof(std::uint32_t);

    auto new_str_sz = str_sz_1 + str_sz_2 + 1; // "/" between

    auto new_components = parent.get_components() + child.get_components();
    auto new_sz = sizeof(std::uint32_t) + new_components + new_str_sz + 1;
    auto new_ptr = Traits::allocate(allocator, new_sz);
    auto new_u32_ptr = reinterpret_cast<std::uint32_t *>(new_ptr);
    *new_u32_ptr++ = new_str_sz;

    auto new_u8_ptr = reinterpret_cast<std::uint8_t *>(new_u32_ptr);
    for (std::uint32_t i = 0; i < parent.get_components(); ++i) {
        *new_u8_ptr++ = *ptr_1++;
    }
    for (std::uint32_t i = 0; i < child.get_components(); ++i) {
        *new_u8_ptr++ = *ptr_2++;
    }

    std::memcpy(new_u8_ptr, ptr_1, str_sz_1);
    new_u8_ptr += str_sz_1;
    *new_u8_ptr++ = '/';

    std::memcpy(new_u8_ptr, ptr_2, str_sz_2);
    new_u8_ptr += str_sz_2;
    *new_u8_ptr++ = 0;
    return path_view_t(new_ptr, new_components, allocator);
}

template <typename Allocator> auto path_base_t::get_view(const Allocator &a) const noexcept -> path_view_t<Allocator> {
    return path_view_t<Allocator>(*this, a);
};

template <typename Allocator>
auto make_view(std::string_view normalized_path, const Allocator &a) noexcept -> path_view_t<Allocator> {
    return path_view_t<Allocator>(normalized_path, a);
};

using allocator_t = std::pmr::polymorphic_allocator<char>;
using poly_path_view_t = path_view_t<allocator_t>;

} // namespace syncspirit::model
