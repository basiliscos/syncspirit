#pragma once

#include "path.h"
#include <cstdint>
#include <string_view>
#include <boost/nowide/convert.hpp>
#include <memory_resource>
#include <cassert>

namespace syncspirit::model {

namespace details {

using pieces_t = std::pmr::vector<std::uint8_t>;
using positions_t = std::pmr::vector<size_t>;

template <typename CharT> struct support_t;

struct result_1_t {
    std::uint32_t sz;
    pieces_t pieces;
    positions_t backslashes;
};

template <> struct support_t<char> {
    using str_t = std::string_view;
    using traits_in_t = boost::nowide::utf::utf_traits<char>;

    static void copy(str_t src, std::uint8_t *dst_, const pieces_t &, const positions_t &backslashes) noexcept {
        auto dst = dst_;
        for (auto c : src) {
            *dst++ = static_cast<uint8_t>(c);
        }
        for (auto p : backslashes) {
            dst_[p] = '/';
        }
        *dst = 0;
    }

    template <typename Allocator> static result_1_t first_pass(str_t in, const Allocator &allocator_) noexcept {
        using namespace boost::nowide;
        auto allocator = allocator_;
        auto pieces = pieces_t(allocator);
        auto backslashes = positions_t(allocator);
        auto begin = in.data();
        auto ptr = begin;
        auto prev = ptr;
        auto end = ptr + in.size();
        auto idx = (unsigned char){0};
        while (ptr != end) {
            auto b = ptr;
            auto c = traits_in_t::decode(ptr, end);
            if (c == utf::illegal || c == utf::incomplete) {
                return {};
            } else {
                if (ptr - b == 1) {
                    if (*b == '/' || *b == '\\') {
                        auto delta = ptr - prev;
                        if (delta > 255) {
                            return {};
                        }
                        pieces.push_back(static_cast<unsigned char>(delta));
                        prev = ptr;
                        if (*b == '\\') {
                            backslashes.push_back(static_cast<std::size_t>(b - begin));
                        }
                    }
                }
            }
        }
        auto sz = static_cast<std::uint32_t>(in.size());
        return {sz, pieces, backslashes};
    }
};

template <> struct support_t<wchar_t> {
    using str_t = std::wstring_view;
    using traits_in_t = boost::nowide::utf::utf_traits<wchar_t>;
    using traits_out_t = boost::nowide::utf::utf_traits<char>;

    static void copy(str_t src, std::uint8_t *dst_, const pieces_t &pieces, const positions_t &backslashes) noexcept {
        auto in = src.data();
        auto in_end = src.data() + src.size();
        auto dst = reinterpret_cast<char *>(dst_);
        for (size_t i = 0; i < src.size(); ++i) {
            auto c = traits_in_t::decode(in, in_end);
            if (c == L'\\') {
                *dst++ = '/';
            } else {
                dst = traits_out_t::encode(c, dst);
            }
        }
        *dst = 0;
    }

    template <typename Allocator> static result_1_t first_pass(str_t in, const Allocator &allocator_) noexcept {
        using namespace boost::nowide;
        auto allocator = allocator_;
        auto pieces = pieces_t(allocator);
        auto backslashes = positions_t(allocator);
        auto begin = in.data();
        auto ptr = begin;
        auto prev = ptr;
        auto end = ptr + in.size();
        auto idx = (unsigned char){0};
        auto sz = std::uint32_t{0};
        while (ptr != end) {
            auto b = ptr;
            auto c = traits_in_t::decode(ptr, end);
            if (c == utf::illegal || c == utf::incomplete) {
                return {};
            } else {
                char out[4];
                auto out_begin = &out[0];
                auto out_ptr = traits_out_t::encode(c, out_begin);
                sz += static_cast<std::uint32_t>(out_ptr - out_begin);

                if (ptr - b == 1) {
                    if (*b == '/' || *b == '\\') {
                        auto delta = ptr - prev;
                        if (delta > 255) {
                            return {};
                        }
                        pieces.push_back(static_cast<unsigned char>(delta));
                        prev = ptr;
                        if (*b == '\\') {
                            backslashes.push_back(static_cast<std::size_t>(b - begin));
                        }
                    }
                }
            }
        }
        return {sz, pieces, backslashes};
    }
};

} // namespace details

struct path_decomposer_t {
    struct content_t {
        const void *data = nullptr;
        std::uint32_t components{0};
    };

    template <bool use_new, typename CharT, typename Allocator>
    static content_t decompose(std::basic_string_view<CharT> full_name, const Allocator &allocator_) noexcept {
        using namespace boost::nowide;
        using namespace boost::nowide::utf;

        using traits_in_t = utf_traits<char>;
        using traits_out_t = utf_traits<wchar_t>;
        using support_t = details::support_t<CharT>;

        if (!full_name.empty()) {
            auto [str_sz, pieces, backslashes] = support_t::first_pass(full_name, allocator_);
            if (!str_sz) {
                return {};
            }
            auto allocator = allocator_;

            auto sz = sizeof(std::uint32_t) + pieces.size() + str_sz + 1;
            auto data_ptr = (uint8_t *){};
            if constexpr (use_new) {
                data_ptr = static_cast<uint8_t *>(::operator new(sz, path_base_t::path_alignment));
            } else {
                using Traits = std::allocator_traits<Allocator>;
                data_ptr = reinterpret_cast<uint8_t *>(Traits::allocate(allocator, sz));
            }
            auto raw_u32 = reinterpret_cast<std::uint32_t *>(data_ptr);
            *raw_u32++ = str_sz;
            auto raw_u8_ptr = reinterpret_cast<std::uint8_t *>(raw_u32);
            for (auto p : pieces) {
                *raw_u8_ptr++ = p;
            }

            support_t::copy(full_name, raw_u8_ptr, pieces, backslashes);
            return {data_ptr, static_cast<std::uint32_t>(pieces.size())};
        }
        return {};
    }
};

} // namespace syncspirit::model
