// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "path_utils.h"
#include "path_view.hpp"

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <ftw.h>
#include <fcntl.h>
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
        if (errno != ENOENT) {
            ec = std::error_code{errno, std::system_category()};
        }
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
    ec = {};
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


void remove_all(const poly_path_view_t &path, std::error_code &ec) noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#error "TODO"
#else
    auto cb = [](const char* path, const struct stat *sb, int type, struct FTW *ftwbuf) -> int {
        if (type == FTW_DP) {
             if (rmdir(path) != 0) {
                 return -1;
             }
        } else {
            if (unlink(path) != 0) {
                return -1;
            }
        }
        return 0;
    };
    auto seed = path.get_full_name();
    auto code = nftw(seed.data(), cb, 128, FTW_DEPTH | FTW_PHYS);
    if (code != 0) {
        ec = std::error_code(code, std::system_category());
    }
#endif
}

void rename(const utils::path_base_t &from, const utils::poly_path_view_t &to, std::error_code &ec) noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    auto f = from.get_view(to.get_allocator());
    auto wf = f.get_full_wname(true);
    auto wt = to.get_full_wname(true);
    auto code = ::MoveFileW(wf.data(), wt.data());
    if (code == 0) {
        ec = sys::error_code(::GetLastError(), std::system_category());
    }
#else
    auto code = ::rename(from.get_full_name().data(), to.get_full_name().data());
    if (code != 0) {
        ec = std::error_code{errno, std::system_category()};
    }
#endif
}

void remove_file(const poly_path_view_t &path, std::error_code &ec) noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    auto wf = path.get_full_wname(true);
    if (!DeleteFileW(wf.data())) {
        ec = sys::error_code(::GetLastError(), std::system_category());
    }
#else
    if (unlink(path.get_full_name().data()) != 0) {
        ec = std::error_code{errno, std::system_category()};
    }
#endif
}

void  chmod(const path_base_t &path, std::uint32_t perms, std::error_code &ec) noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    ec = std::make_error_code(std::errc::function_not_supported);
#else
    if (::chmod(path.get_full_name().data(), perms) !=0) {
        ec = std::error_code{errno, std::system_category()};
    }
#endif
}

void create_symlink(const utils::path_base_t &target, const utils::path_base_t &path, std::error_code &ec) noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    ec = std::make_error_code(std::errc::function_not_supported);
#else
    if (::symlink(target.get_full_name().data(), path.get_full_name().data()) !=0 ) {
        ec = std::error_code{errno, std::system_category()};
    }
#endif
}

bool is_symlink(const utils::path_base_t &target, std::error_code &ec) noexcept {
    auto r = false;
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    ec = std::make_error_code(std::errc::function_not_supported);
#else
    struct stat st;
    if (lstat(target.get_full_name().data(), &st) != 0) {
        ec = std::error_code{errno, std::system_category()};
        r = false;
    }
    r = S_ISLNK(st.st_mode);
#endif
    return r;
}

poly_string_t read_symlink(const poly_path_view_t &target, std::error_code &ec) noexcept {
    auto storage = std::pmr::string(target.get_allocator());
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    ec = std::make_error_code(std::errc::function_not_supported);
#else
    storage.resize(PATH_MAX);
    auto code = readlink(target.get_full_name().data(), storage.data(), storage.size());
    if (code == -1) {
        ec = std::error_code{errno, std::system_category()};
        storage = {};
    } else {
        storage.resize(static_cast<size_t>(code));
    }
#endif
    return storage;
}

void last_write_time(const poly_path_view_t &path, std::int64_t modified_at, std::error_code &ec) noexcept {
    struct timespec times[2];
    times[0].tv_nsec = UTIME_OMIT;          /* keep atime */
    times[1].tv_sec  = modified_at;
    times[1].tv_nsec = 0;
    if (::utimensat(AT_FDCWD, path.get_full_name().data(), times, AT_SYMLINK_NOFOLLOW) == -1) {
        ec = std::error_code{errno, std::system_category()};
    }
}

std::int64_t last_write_time(const poly_path_view_t &path, std::error_code &ec) noexcept {
    return get_stats(path, ec).modification;
}

stats_t get_stats(const poly_path_view_t &path, std::error_code &ec) noexcept {
    stats_t r;
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#error TODO
#else
    struct stat st;
    if (lstat(path.get_full_name().data(), &st) != 0) {
        ec = std::error_code{errno, std::system_category()};
    } else {
        r.supported = true;
        r.modification = st.st_mtim.tv_sec;
        r.permissions = st.st_mode & 07777;
        if (S_ISDIR(st.st_mode)) {
            r.file_type = file_type_t::DIRECTORY;
        } else if (S_ISREG(st.st_mode)) {
            r.file_type = file_type_t::FILE;
            r.file_size = st.st_size;
        } else if (S_ISLNK(st.st_mode)) {
            r.file_type = file_type_t::SYMLINK;
        } else {
            r.supported = false;
        }

    }
#endif
    return r;
}



} // namespace syncspirit::utils
