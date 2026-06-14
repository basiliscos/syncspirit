// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#pragma once

#include <catch2/catch_test_macros.hpp>

#include "model/device.h"
#include "model/cluster.h"
#include "model/diff/apply_controller.h"
#include "model/diff/cluster_diff.h"
#include "proto/proto-helpers.h"
#include "utils/path.h"
#include "utils/path_view.hpp"
#include "syncspirit-test-export.h"

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#define SYNCSPIRIT_WIN
#endif
#if defined(__APPLE__)
#define SYNCSPIRIT_MAC
#endif

namespace syncspirit::utils {

template <typename T, typename TP = std::remove_reference_t<std::remove_cv_t<T>>,
          typename CharT = typename std::char_traits<typename TP::value_type>::char_type,
          typename = std::enable_if_t<std::is_convertible_v<T &&, std::basic_string_view<CharT>>>>
inline poly_path_view_t operator/(const poly_path_view_t& path, T &&normalized_path) {
    return path / make_native_view(normalized_path, path.get_allocator());
}

inline poly_path_view_t operator/(const poly_path_view_t& path, const char* normalized_path) {
    return path / std::string_view(normalized_path);
}

inline poly_path_view_t operator/(const poly_path_view_t& path, const wchar_t* normalized_path) {
    return path / std::wstring_view(normalized_path);
}

}

namespace syncspirit::test {

namespace sys = boost::system;

struct test_apply_controller_t final : model::arc_base_t<test_apply_controller_t>, model::diff::apply_controller_t {
    inline test_apply_controller_t(model::cluster_ptr_t cluster_) { cluster = cluster_; }
};

using apply_controller_ptr_t = model::intrusive_ptr_t<test_apply_controller_t>;

struct SYNCSPIRIT_TEST_API path_guard_t: utils::path_t {
    using utils::path_t::path_t;
    path_guard_t();
    path_guard_t(std::wstring path);
    path_guard_t(path_guard_t &) = delete;
    path_guard_t(path_guard_t &&) = default;
    ~path_guard_t();
};

SYNCSPIRIT_TEST_API utils::bytes_view_t as_bytes(std::string_view);
SYNCSPIRIT_TEST_API utils::bytes_t as_owned_bytes(std::string_view);
SYNCSPIRIT_TEST_API path_guard_t unique_path();
SYNCSPIRIT_TEST_API apply_controller_ptr_t make_apply_controller(model::cluster_ptr_t cluster);
SYNCSPIRIT_TEST_API void init_logging();
SYNCSPIRIT_TEST_API utils::poly_path_view_t locate_path(const char *test_file, const utils::allocator_t&);
SYNCSPIRIT_TEST_API std::string read_file(const utils::poly_path_view_t &path);
SYNCSPIRIT_TEST_API void write_file(const utils::poly_path_view_t &path, std::string_view content);
SYNCSPIRIT_TEST_API utils::bytes_t device_id2sha256(std::string_view device_id);
SYNCSPIRIT_TEST_API model::device_ptr_t make_device(std::string_view device_id, std::string_view name = "");
SYNCSPIRIT_TEST_API std::string hash_string(const std::string_view &hash) noexcept;
SYNCSPIRIT_TEST_API bool has_ipv6() noexcept;
SYNCSPIRIT_TEST_API utils::bytes_t make_key(model::block_info_ptr_t block);
SYNCSPIRIT_TEST_API bool wine_environment();

SYNCSPIRIT_TEST_API std::size_t create_directories(const utils::poly_path_view_t &path);
SYNCSPIRIT_TEST_API bool exists(const utils::poly_path_view_t &path);
SYNCSPIRIT_TEST_API void chmod(const utils::path_base_t &path, std::uint32_t);
SYNCSPIRIT_TEST_API std::int64_t file_size(const utils::poly_path_view_t &path);
SYNCSPIRIT_TEST_API std::int64_t last_write_time(const utils::poly_path_view_t &path);
SYNCSPIRIT_TEST_API void last_write_time(const utils::poly_path_view_t &path, std::int64_t);
SYNCSPIRIT_TEST_API bool is_directory(const utils::poly_path_view_t &path);
SYNCSPIRIT_TEST_API bool is_symlink(const utils::poly_path_view_t &path);
SYNCSPIRIT_TEST_API std::uint32_t permissions(const utils::poly_path_view_t &path);
SYNCSPIRIT_TEST_API utils::poly_string_t read_symlink(const utils::poly_path_view_t &target);
SYNCSPIRIT_TEST_API void rename(const utils::path_base_t &from, const utils::poly_path_view_t &to);
SYNCSPIRIT_TEST_API void create_symlink(const utils::path_base_t &target, const utils::path_base_t &path);
SYNCSPIRIT_TEST_API void remove(const utils::poly_path_view_t &path);
SYNCSPIRIT_TEST_API void remove_all(const utils::poly_path_view_t &path);

} // namespace syncspirit::test
