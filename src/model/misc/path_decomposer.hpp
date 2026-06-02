#pragma once

#include "path.h"
#include <cstdint>
#include <string_view>
#include <boost/nowide/convert.hpp>
#include <memory_resource>
#include <cassert>

namespace syncspirit::model {

struct path_decomposer_t {
    struct content_t {
        const void *data = nullptr;
        std::uint32_t components{0};
    };

    template <bool use_new, typename CharT, typename Allocator>
    static content_t decompose(std:: basic_string_view<CharT> full_name, const Allocator &allocator_) noexcept {
        using namespace boost::nowide;
        using namespace boost::nowide::utf;

        using traits_in_t = utf_traits<char>;
        using traits_out_t = utf_traits<wchar_t>;
        using pieces_t = std::pmr::vector<unsigned char>;
        using positions_t = std::pmr::vector<size_t>;

        auto allocator = allocator_;
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
                    return {};
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
            auto data_ptr = (uint8_t *){};
            if constexpr (use_new) {
                data_ptr = static_cast<uint8_t *>(::operator new(sz, path_base_t::path_alignment));
            } else {
                using Traits = std::allocator_traits<Allocator>;
                data_ptr = reinterpret_cast<uint8_t *>(Traits::allocate(allocator, sz));
            }
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
            return {data_ptr, pieces_number};
        }
        return {};
    }
};

} // namespace syncspirit::model
