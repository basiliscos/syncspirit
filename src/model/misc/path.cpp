// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "path.h"
#include "fs/utils.h"
#include <boost/nowide/convert.hpp>
#include <cstring>
#include <cassert>
#include <memory_resource>

namespace bfs = std::filesystem;

using namespace boost::nowide;
using namespace boost::nowide::utf;
using namespace syncspirit::model;

using I = path_t::iterator_t;

path_t::path_t(path_t &&other) noexcept {
    std::swap(data, other.data);
    std::swap(components, other.components);
}

path_t::path_t(std::string_view full_name) noexcept {
    using traits_in_t = utf_traits<char>;
    using traits_out_t = utf_traits<wchar_t>;
    using pieces_t = std::pmr::vector<unsigned char>;
    using positions_t = std::pmr::vector<size_t>;

    auto buffer = std::array<std::byte, 1024 * 5>{};
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto pieces = pieces_t(allocator);
    auto backslashes = positions_t(allocator);
    auto pieces_number = std::uint32_t{0};

    if (!full_name.empty()) {
        auto begin = full_name.data();
        auto ptr = begin;
        auto prev = ptr;
        auto end = ptr + full_name.size();
        auto idx = (unsigned char){0};
        while (ptr != end) {
            auto b = ptr;
            auto c = traits_in_t::decode(ptr, end);
            if (c == illegal || c == incomplete) {
                return;
            } else {
                if (ptr - b == 1) {
                    if (*b == '/' || *b == '\\') {
                        ++pieces_number;
                        auto delta = ptr - prev;
                        assert(delta <= 255);
                        pieces.push_back(static_cast<unsigned char>(delta));
                        prev = ptr;
                        if (*b == '\\') {
                            backslashes.push_back(static_cast<std::size_t>(b - begin));
                        }
                    }
                }
            }
        }
        auto sz = sizeof(std::uint32_t) + pieces.size() + full_name.size() + 1;
        auto data_ptr = static_cast<uint8_t *>(::operator new(sz, path_alignment));
        auto raw_u32 = reinterpret_cast<std::uint32_t *>(data_ptr);
        *raw_u32++ = full_name.size();
        auto raw_u8_ptr = reinterpret_cast<std::uint8_t *>(raw_u32);
        for (auto p : pieces) {
            *raw_u8_ptr++ = p;
        }
        auto raw_u8 = raw_u8_ptr;
        for (auto c : full_name) {
            *raw_u8_ptr++ = static_cast<uint8_t>(c);
        }
        for (auto p : backslashes) {
            raw_u8[p] = '/';
        }
        *raw_u8_ptr = 0;
        data = data_ptr;
        components = pieces_number;
    }
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

bool path_base_t::empty() const noexcept { return components == 0; }

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
            auto first_letter = (ptr[0] >= 'a' && ptr[0] <= 'z') ||  (ptr[0] >= 'A' && ptr[0] <= 'Z');
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
