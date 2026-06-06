// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "path_utils.h"
#include "path_view.hpp"

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#include <windows.h>
#else
#include <sys/stat.h>
#include <errno.h>
#endif

namespace syncspirit::utils {

bool exists(const poly_path_view_t &path, std::error_code &ec) noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    auto wpath = path.get_full_wname(true);
    auto attrs = GetFileAttributesW(wpath.data());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        ec = std::error_code(::GetLastError(), std::system_category());
        return false;
    }
    return attrs & FILE_ATTRIBUTE_DIRECTORY;
#else
    struct stat data;
    if (lstat(path.get_full_name().data(), &data) == 0) {
        return true;
    } else {
        ec = std::error_code{errno, std::system_category()};
        return false;
    }
#endif
}

bool is_empty(const poly_path_view_t &path, std::error_code &ec) noexcept { std::abort(); }

std::size_t create_directories(const poly_path_view_t &path, std::error_code &ec) noexcept {
    auto r = std::size_t{0};
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    auto whole_str = std::pmr::wstring(path.get_allocator());
    whole_str = path.get_full_wname(true);
    auto pos = whole_str.find(L'\\', 0);
    while (pos != std::wstring::npos) {
        whole_str[pos] = 0;
        auto code = _wmkdir(whole_str.data());
        whole_str[pos] = L'\\';
        if (!code) {
            ++r;
        } else {
            if (errno == EEXIST) {
                if (pos < whole_str.size()) {
                    pos = whole_str.find(L'\\', pos + 1);
                    if (pos == std::wstring::npos) {
                        pos = whole_str.size();
                    }
                } else {
                    printf("zzz1.3.2\n");
                    pos = std::wstring::npos;
                }
            } else {
                ec = std::error_code(::GetLastError(), std::system_category());
                break;
            }
        }
    }
#else
    static constexpr auto perms = 0777;
    auto whole = path.get_full_name();
    auto whole_str = std::pmr::string(path.get_allocator());
    whole_str = whole;
    auto partial_ptr = whole_str.data();

    for (auto piece : path) {
        auto partial_end = piece.data() + piece.size();
        auto partial_sz = partial_end - whole.data();
        if (partial_sz) {
            partial_ptr[partial_sz] = 0;
            auto ok = mkdir(partial_ptr, perms) == 0;
            if (!ok) {
                auto code = errno;
                if (code != EEXIST) {
                    ec = std::error_code(code, std::system_category());
                    break;
                }
            } else {
                ++r;
            }
            partial_ptr[partial_sz] = '/';
        }
    }
#endif
    return r;
}

} // namespace syncspirit::utils
