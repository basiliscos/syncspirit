// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "platform.h"
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#include <array>
#include <zlib.h>
#endif

using namespace syncspirit::utils;

bool platform_t::startup() {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    // make explicit dependency on zlib, as openssl loads it via LoadLibrary,
    // and it cannot be found by scanning dlls
    (void)zlibVersion(); // make explict

    auto wVersionRequested = MAKEWORD(2, 2);
    WSADATA wsaData;

    auto err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) {
        return false;
    }
#endif
    return true;
}

void platform_t::shutdown() noexcept {}

bool platform_t::symlinks_supported() noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    return false;
#else
    return true;
#endif
}

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
namespace {

using names_t = std::array<std::wstring_view, 1 + 13 + 1 + 1 + 13 + 1>;

// clang-format off
names_t reserved_names = {
    L"aux",
    L"com0", L"com1", L"com2", L"com3", L"com4", L"com5", L"com6", L"com7", L"com8", L"com9", L"com²", L"com³", L"com¹",
    L"con",
    L"lpt0", L"lpt1", L"lpt2", L"lpt3", L"lpt4", L"lpt5", L"lpt6", L"lpt7", L"lpt8", L"lpt9",L"lpt²",  L"lpt³",  L"lpt¹",
    L"nul",
    L"prn",
};
// clang-format on

struct range_t {
    int a;
    int b;
    bool is_valid() const { return a <= b; };
};

range_t bisect(wchar_t needle, int offset, range_t r) {
    int a = r.a;
    bool found_a = false;

    for (; a <= r.b; ++a) {
        auto &source = reserved_names[a];
        if (offset >= static_cast<int>(source.size())) {
            continue;
        }
        auto symbol = source[offset];
        if (symbol == needle) {
            found_a = true;
            break;
        }
        if (symbol > needle) {
            break;
        }
    }

    if (!found_a) {
        return {0, -1};
    }

    int b = a;
    bool overmatch_b = false;
    for (; b <= r.b; ++b) {
        auto &source = reserved_names[b];
        if (offset >= static_cast<int>(source.size())) {
            overmatch_b = true;
            break;
        }
        auto symbol = source[offset];
        if (symbol > needle) {
            overmatch_b = true;
            break;
        }
    }
    if (b > r.b)
        overmatch_b = true;
    return {a, b - (overmatch_b ? 1 : 0)};
}

} // namespace
#endif

bool platform_t::path_supported(const bfs::path &path) noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    for (auto it = path.begin(); it != path.end(); ++it) {
        auto name = it->stem().wstring();
        auto range = range_t{0, static_cast<int>(reserved_names.size()) - 1};
        for (size_t i = 0; i < name.size(); ++i) {
            auto symbol = name[i];
            if (symbol < 31) {
                return false;
            }
            switch (symbol) {
                // clang-format off
                case L'<':
                case L'>':
                case L':':
                case L'"':
                case L'\\':
                case L'/':
                case L'|':
                case L'?':
                case L'*':
                    return false;
                case L'A': symbol = 'a'; break;
                case L'C': symbol = 'c'; break;
                case L'L': symbol = 'l'; break;
                case L'M': symbol = 'M'; break;
                case L'N': symbol = 'n'; break;
                case L'O': symbol = 'o'; break;
                case L'P': symbol = 'p'; break;
                case L'R': symbol = 'r'; break;
                case L'T': symbol = 't'; break;
                case L'X': symbol = 'x'; break;
                default: /* noop */ ;
                // clang-format on
            }
            range = bisect(symbol, static_cast<int>(i), range);
            if (!range.is_valid()) {
                break;
            }
            if (range.a == range.b && ((i + 1) == name.size())) {
                auto &reserved = reserved_names[range.a];
                if (reserved.size() == name.size()) {
                    return false;
                }
                break;
            }
        }
    }
#endif

    (void)path;
    return true;
}

bool platform_t::permissions_supported(const bfs::path &) noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    return false;
#endif
    return true;
}
