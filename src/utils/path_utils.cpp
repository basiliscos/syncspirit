// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "path_utils.h"
#include "path_view.hpp"

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#define _CRT_STDIO_ISO_WIDE_SPECIFIERS 1 /* optional on some toolchains */
#include <windows.h>
#include <sys/types.h>
#include <sys/utime.h>
#include <sys/stat.h>
#include <time.h>
#include <inttypes.h>
#else
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <ftw.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>
#endif

// #include <spdlog/spdlog.h>
#include "syncspirit-config.h"
#include "fs/utils.h"

namespace syncspirit::utils {

bool is_temporal(std::string_view path) noexcept {
    if (path.size() > fs::tmp_suffix.size()) {
        auto tail = path.substr(path.size() - fs::tmp_suffix.size());
        return tail == fs::tmp_suffix;
    }
    return false;
}

bool exists(const poly_path_view_t &path, std::error_code &ec) noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    (void)ec;
    auto wpath = path.get_full_wname(true);
    auto attrs = GetFileAttributesW(wpath.data());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return true;
#else
    struct stat data;
    if (lstat(path.get_full_name().data(), &data) == 0) {
        return true;
    } else {
        if (errno != ENOENT) {
            ec = std::error_code{errno, std::generic_category()};
        }
        return false;
    }
#endif
}

bool is_empty(const poly_path_view_t &path, std::error_code &ec) noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    auto mask = path / utils::make_native_view("*.*", path.get_allocator());
    auto wpath = mask.get_full_wname(true);
    WIN32_FIND_DATAW child_data;
    auto child_handle = FindFirstFileW(wpath.data(), &child_data);
    if (child_handle == INVALID_HANDLE_VALUE) {
        ec = std::error_code(::GetLastError(), std::system_category());
        return false;
    }
    auto r = true;
    do {
        auto child_name = std::wstring_view(child_data.cFileName);
        if (child_name != L"." && child_name != L"..") {
            r = false;
            break;
        }
    } while (FindNextFileW(child_handle, &child_data) && !ec);
    if (!FindClose(child_handle)) {
        ec = std::error_code(::GetLastError(), std::system_category());
    }
    return r;

#else
    auto d = ::opendir(path.get_full_name().data());
    if (!d) {
        ec = std::error_code{errno, std::generic_category()};
        return false;
    }
    auto r = true;
    auto entry = readdir(d);
    while (entry) {
        auto name = std::string_view(entry->d_name);
        if (!(name == "." || name == "..")) {
            r = false;
            break;
        }
        entry = readdir(d);
    }
    if (closedir(d) != 0) {
        ec = std::error_code{errno, std::generic_category()};
        r = false;
    }
    return r;
#endif
}

std::size_t create_directories(const poly_path_view_t &path, std::error_code &ec) noexcept {
    auto r = std::size_t{0};
    ec = {};
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    auto whole_str = std::pmr::wstring(path.get_allocator());
    whole_str = path.get_full_wname(true);
    auto pos = std::size_t{0};
    if (whole_str[0] == L'\\' && whole_str.size() > 8) {
        pos += 8; // skip "\\?\X:\"
    } else if (path.is_absolute() && whole_str.size() > 3) {
        pos += 3;
    }
    pos = whole_str.find(L'\\', pos);
    bool advance = true;
    while (advance) {
        if (pos != std::wstring::npos) {
            whole_str[pos] = 0;
        }
        auto code = _wmkdir(whole_str.data());
        if (pos != std::wstring::npos) {
            whole_str[pos] = L'\\';
        }
        if (!code) {
            ++r;
        } else {
            if (errno != EEXIST) {
                ec = std::error_code(::GetLastError(), std::system_category());
                advance = false;
            }
        }
        if (advance) {
            if (pos < whole_str.size()) {
                pos = whole_str.find(L'\\', pos + 1);
                advance = true;
            } else {
                advance = false;
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
                    ec = std::error_code(code, std::generic_category());
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

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
void rm_file(std::wstring_view path, std::error_code &ec) noexcept {
    if (SetFileAttributesW(path.data(), FILE_ATTRIBUTE_NORMAL) == 0) {
        ec = std::error_code(::GetLastError(), std::system_category());
    } else {
        if (DeleteFileW(path.data()) == 0) {
            ec = std::error_code(::GetLastError(), std::system_category());
        }
    }
}

void rm_dir_recurse(std::wstring_view path, std::error_code &ec) noexcept {
    auto ptr = const_cast<wchar_t *>(path.data() + path.size());
    auto child_ptr = ptr;
    swprintf(ptr, L"\\*.*");
    WIN32_FIND_DATAW child_data;
    auto child_handle = FindFirstFileW(path.data(), &child_data);
    if (child_handle != INVALID_HANDLE_VALUE) {
        do {
            auto child_name = std::wstring_view(child_data.cFileName);
            if (child_name != L"." && child_name != L"..") {
                swprintf(ptr, L"\\%ls", child_name.data());
                auto full_name = std::wstring_view(path.data(), path.size() + child_name.size() + 1);
                if (child_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    rm_dir_recurse(full_name, ec);
                } else {
                    rm_file(full_name, ec);
                }
            }
            swprintf(ptr, L"\\*.*");
        } while (FindNextFileW(child_handle, &child_data) && !ec);

        if (!FindClose(child_handle)) {
            ec = std::error_code(::GetLastError(), std::system_category());
        }
    }

    if (!ec) {
        *ptr = 0;
        if (RemoveDirectoryW(path.data()) == 0) {
            ec = std::error_code(::GetLastError(), std::system_category());
        }
    }
}

void rm_dir_recurse_initial(std::wstring_view wpath, std::error_code &ec) noexcept {
    wchar_t buff[SYNCSPIRIT_PATH_MAX + 16];
    memcpy(buff, wpath.data(), wpath.size() * sizeof(wchar_t));
    auto ptr = buff + wpath.size();
    *ptr = 0;
    rm_dir_recurse(std::wstring_view(buff, wpath.size()), ec);
}

#endif

void remove_all(const poly_path_view_t &path, std::error_code &ec) noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    auto wpath = path.get_full_wname(true);
    auto attrs = GetFileAttributesW(wpath.data());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        ec = std::error_code(::GetLastError(), std::system_category());
    } else {
        auto wview = std::wstring_view(wpath.data(), wpath.size());
        if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
            rm_dir_recurse_initial(wview, ec);
        } else {
            rm_file(wview, ec);
        }
    }
#else
    auto orig_path = path.get_full_name();
    struct stat st;
    if (lstat(orig_path.data(), &st) != 0) {
        ec = std::error_code{errno, std::generic_category()};
    } else {
        if (S_ISDIR(st.st_mode)) {
            auto cb = [](const char *path, const struct stat *sb, int type, struct FTW *ftwbuf) -> int {
                if (type == FTW_DP) {
                    if (rmdir(path) != 0) {
                        return -1;
                    }
                } else if (type == FTW_F || type == FTW_SL || type == FTW_SLN) {
                    if (unlink(path) != 0) {
                        return -1;
                    }
                } else {
                    errno = EIO;
                    return -1;
                }
                return 0;
            };

            errno = 0;
            auto code = nftw(orig_path.data(), cb, 128, FTW_DEPTH | FTW_PHYS);
            if (code != 0) {
                ec = std::error_code(errno, std::generic_category());
            }
        } else {
            if (unlink(orig_path.data()) != 0) {
                ec = std::error_code(errno, std::generic_category());
            }
        }
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
        ec = std::error_code(::GetLastError(), std::system_category());
    }
#else
    auto code = ::rename(from.get_full_name().data(), to.get_full_name().data());
    if (code != 0) {
        ec = std::error_code{errno, std::generic_category()};
    }
#endif
}

void remove_file(const poly_path_view_t &path, std::error_code &ec) noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    auto wf = path.get_full_wname(true);
    if (!DeleteFileW(wf.data())) {
        ec = std::error_code(::GetLastError(), std::system_category());
    }
#else
    if (unlink(path.get_full_name().data()) != 0) {
        ec = std::error_code{errno, std::generic_category()};
    }
#endif
}

void chmod(const poly_path_view_t &path, std::uint32_t perms, std::error_code &ec) noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    auto wf = path.get_full_wname(true);
    if (_wchmod(wf.data(), perms) != 0) {
        ec = std::error_code{errno, std::system_category()};
    }
#else
    if (::chmod(path.get_full_name().data(), perms) != 0) {
        ec = std::error_code{errno, std::generic_category()};
    }
#endif
}

void create_symlink(const utils::path_base_t &target, const utils::path_base_t &path, std::error_code &ec) noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    ec = std::make_error_code(std::errc::function_not_supported);
#else
    if (::symlink(target.get_full_name().data(), path.get_full_name().data()) != 0) {
        ec = std::error_code{errno, std::generic_category()};
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
        ec = std::error_code{errno, std::generic_category()};
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
    storage.resize(SYNCSPIRIT_PATH_MAX);
    auto code = readlink(target.get_full_name().data(), storage.data(), storage.size());
    if (code == -1) {
        ec = std::error_code{errno, std::generic_category()};
        storage = {};
    } else {
        storage.resize(static_cast<size_t>(code));
    }
#endif
    return storage;
}

void last_write_time(const poly_path_view_t &path, std::int64_t modified_at, std::error_code &ec) noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    auto wpath = path.get_full_wname(true);
    struct _utimbuf times;
    times.actime = 0;
    times.modtime = (time_t)modified_at;
    if (_wutime(wpath.data(), &times) != 0) {
        ec = std::error_code{errno, std::system_category()};
    }
#else
    struct timespec times[2];
    times[0].tv_nsec = UTIME_OMIT; /* keep atime */
    times[1].tv_sec = modified_at;
    times[1].tv_nsec = 0;
    if (::utimensat(AT_FDCWD, path.get_full_name().data(), times, AT_SYMLINK_NOFOLLOW) == -1) {
        ec = std::error_code{errno, std::generic_category()};
    }
#endif
}

std::int64_t last_write_time(const poly_path_view_t &path, std::error_code &ec) noexcept {
    return get_stats(path, ec).modification;
}

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
inline std::int64_t to_unix(const FILETIME &ft) {
    constexpr std::int64_t UNIX_TIME_START = 0x019DB1DED53E8000ll; // January 1, 1970 (start of Unix epoch) in "ticks"
    auto v = ((std::int64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    // convert to seconds since 1601
    auto u = v - UNIX_TIME_START;
    return u / 10000000ULL;
}
#endif

stats_t get_stats(const poly_path_view_t &path, std::error_code &ec) noexcept {
    stats_t r;
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    auto wpath = path.get_full_wname(true);
    struct __stat64 st;
    if (_wstat64(wpath.data(), &st) != 0) {
        ec = std::error_code{errno, std::system_category()};
    } else {
        r.supported = true;
        r.permissions = st.st_mode & 07777;
        if (st.st_mode & _S_IFDIR) {
            r.file_type = file_type_t::DIRECTORY;
        } else if (st.st_mode & _S_IFREG) {
            r.file_type = file_type_t::FILE;
            r.file_size = st.st_size;
        } else {
            r.supported = false;
        }
        if (r.supported) {
            // buggy st.st_mtime might contain daylight saving time application
            auto data_w = WIN32_FIND_DATAW{};
            auto h = FindFirstFileW(wpath.data(), &data_w);
            if (h != INVALID_HANDLE_VALUE) {
                r.modification = to_unix(data_w.ftLastWriteTime);
                FindClose(h);
            }
        }
    }
#else
    struct stat st;
    if (lstat(path.get_full_name().data(), &st) != 0) {
        ec = std::error_code{errno, std::generic_category()};
    } else {
        r.supported = true;
        r.modification = st.st_mtime;
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

poly_path_view_t cwd(const allocator_t &allocator, std::error_code &ec) noexcept {
    auto path = utils::make_empty_view(allocator);
#ifdef SYNCSPIRIT_WIN
    wchar_t buff[MAX_PATH];
    if (!::GetCurrentDirectoryW(sizeof(buff), buff)) {
        ec = std::error_code(::GetLastError(), std::system_category());
    } else {
        path = utils::make_native_view(buff, allocator);
    }
#else
    char buff[SYNCSPIRIT_PATH_MAX];
    if (!getcwd(buff, sizeof(buff))) {
        ec = std::error_code(errno, std::generic_category());
    } else {
        path = utils::make_native_view(buff, allocator);
    }
#endif
    return path;
}

} // namespace syncspirit::utils
