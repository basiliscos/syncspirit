// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "scan_dir.h"
#include "fs/fs_slave.h"
#include "utils/path_view.hpp"
#include "utils/path_utils.h"
#include <algorithm>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#else
#include <sys/types.h>
#include <dirent.h>
#endif

using namespace syncspirit;
using namespace syncspirit::fs;
using namespace syncspirit::fs::task;

// inverse files sorting as files will be inversed again (inderectly) by
// stack structure
struct comparator_t {
    bool operator()(const scan_dir_t::child_info_t &lhs, const scan_dir_t::child_info_t &rhs) const noexcept {
        auto l_dir = lhs.file_type == utils::file_type_t::DIRECTORY;
        auto r_dir = rhs.file_type == utils::file_type_t::DIRECTORY;
        if (l_dir xor r_dir) {
            return l_dir ? false : true;
        } else {
            return lhs.path.get_filename() > rhs.path.get_filename();
        }
    }
};

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
inline std::int64_t to_unix(const FILETIME &ft) {
    constexpr std::int64_t UNIX_TIME_START = 0x019DB1DED53E8000ll; // January 1, 1970 (start of Unix epoch) in "ticks"
    auto v = ((std::int64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    // convert to seconds since 1601
    auto u = v - UNIX_TIME_START;
    return u / 10000000ULL;
}
#endif

scan_dir_t::scan_dir_t(utils::path_t path_, presentation::presence_ptr_t presence_, utils::path_t single_child_,
                       bool notify_, bool recurse_, bool requires_refinement_) noexcept
    : path{std::move(path_)}, presence{std::move(presence_)},
      ec(utils::make_error_code(utils::error_code_t::no_action)), single_child{std::move(single_child_)},
      notify{notify_ ? 1u : 0}, recurse{recurse_ ? 1u : 0}, requires_refinement{requires_refinement_ ? 1u : 0} {}

bool scan_dir_t::process(fs_slave_t &slave, execution_context_t &context) noexcept {
    ec = {};

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    auto wpath = path.get_view(context.allocator).get_full_wname(true);
    wchar_t buff[MAX_PATH];
    memcpy(buff, wpath.data(), wpath.size() * sizeof(wchar_t));
    auto ptr = buff + wpath.size();
    swprintf(ptr, L"\\*.*");
    WIN32_FIND_DATAW child_data;
    auto child_handle = FindFirstFileW(buff, &child_data);
    auto single_child = this->single_child.get_view(context.allocator).get_full_wname(true);
    if (child_handle != INVALID_HANDLE_VALUE) {
        do {
            auto child_name = std::wstring_view(child_data.cFileName);
            if (child_name != L"." && child_name != L"..") {
                if (single_child.empty() || single_child == child_name) {
                    swprintf(ptr, L"\\%ls", child_name.data());
                    auto full_name = std::wstring_view(buff, wpath.size() + child_name.size() + 1);
                    auto child_info = task::scan_dir_t::child_info_t{};
                    auto is_dir = child_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
                    auto sz = static_cast<std::int64_t>((child_data.nFileSizeHigh * (MAXDWORD + 1)) +
                                                        child_data.nFileSizeLow);
                    child_info.path = utils::path_t::make_native(full_name);
                    child_info.file_type = is_dir ? utils::file_type_t::DIRECTORY : utils::file_type_t::FILE;
                    child_info.permissions = 0666;
                    child_info.last_write_time = to_unix(child_data.ftLastWriteTime);
                    child_info.size = sz;
                    child_infos.push_back(std::move(child_info));
                }
            }
        } while (FindNextFileW(child_handle, &child_data) && !ec);

        if (!FindClose(child_handle)) {
            ec = std::error_code(::GetLastError(), std::system_category());
        }
    } else {
        auto code = ::GetLastError();
        if (code == ERROR_PATH_NOT_FOUND || code == ERROR_FILE_NOT_FOUND || code == ERROR_INVALID_DRIVE) {
            ec = std::make_error_code(std::errc::no_such_file_or_directory);
        } else {
            ec = std::error_code(code, std::system_category());
        }
    }
#else
    auto d = ::opendir(path.get_full_name().data());
    auto dir_view = path.get_view(context.allocator);
    if (!d) {
        ec = std::error_code{errno, std::generic_category()};
    } else {
        auto entry = readdir(d);
        while (entry) {
            auto name = std::string_view(entry->d_name);
            if (!(name == "." || name == "..")) {
                if (single_child.empty() || single_child.get_filename() == name) {
                    auto child_path = dir_view / utils::make_native_view(name, context.allocator);
                    auto child_stats = utils::get_stats(child_path, ec);
                    if (!ec && child_stats.supported) {
                        auto child_info = task::scan_dir_t::child_info_t{};
                        child_info.path = child_path.detach();
                        child_info.file_type = child_stats.file_type;
                        child_info.permissions = child_stats.permissions;
                        child_info.last_write_time = child_stats.modification;
                        child_info.size = child_stats.file_size;
                        if (child_stats.file_type == utils::file_type_t::SYMLINK) {
                            auto link = utils::read_symlink(child_path, ec);
                            if (!ec) {
                                child_info.target = utils::path_t::make_native(link);
                            }
                        }
                        child_infos.push_back(std::move(child_info));
                    }
                }
            }
            entry = readdir(d);
        }
        if (closedir(d) != 0) {
            ec = std::error_code{errno, std::generic_category()};
        }
    }
#endif

    auto b = child_infos.begin();
    auto e = child_infos.end();
    std::sort(b, e, comparator_t());

    if (notify && context.scan_dir_callback) {
        context.scan_dir_callback(*this);
    }

    return false;
}
