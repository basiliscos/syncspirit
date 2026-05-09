#pragma once

#include "path.h"
#include <memory>
#include <cstring>

namespace syncspirit::model {

template <typename Allocator> struct path_view_t final : path_base_t {
    using Traits = std::allocator_traits<Allocator>;
    using T = typename Allocator::value_type;

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

  private:
    mutable Allocator allocator;
};

template <typename Allocator>
auto operator/(const path_view_t<Allocator> &parent, const path_view_t<Allocator> &child) noexcept
    -> path_view_t<Allocator> {}

template <typename Allocator> auto path_base_t::get_view(const Allocator &a) const noexcept -> path_view_t<Allocator> {
    return path_view_t<Allocator>(*this, a);
};

} // namespace syncspirit::model
