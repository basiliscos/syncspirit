// SPDX-License-Identifier: GPL-3.0-or-later
/// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include <catch2/catch_all.hpp>
#include <filesystem>

#include "model/device.h"
#include "model/diff/apply_controller.h"
#include "model/diff/cluster_diff.h"
#include "proto/proto-helpers.h"
#include "syncspirit-test-export.h"

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#define SYNCSPIRIT_WIN
#endif

namespace syncspirit::test {

namespace bfs = std::filesystem;
namespace sys = boost::system;

struct SYNCSPIRIT_TEST_API path_guard_t {
    bfs::path path;
    path_guard_t();
    path_guard_t(const bfs::path &path_);
    path_guard_t(path_guard_t &) = delete;
    path_guard_t(path_guard_t &&other);
    ~path_guard_t();
};

SYNCSPIRIT_TEST_API utils::bytes_view_t as_bytes(std::string_view);
SYNCSPIRIT_TEST_API utils::bytes_t as_owned_bytes(std::string_view);
SYNCSPIRIT_TEST_API bfs::path unique_path();
SYNCSPIRIT_TEST_API model::diff::apply_controller_t &get_apply_controller();
SYNCSPIRIT_TEST_API void init_logging();
SYNCSPIRIT_TEST_API bfs::path locate_path(const char *test_file);
SYNCSPIRIT_TEST_API std::string read_file(const bfs::path &path);
SYNCSPIRIT_TEST_API void write_file(const bfs::path &path, std::string_view content);
SYNCSPIRIT_TEST_API utils::bytes_t device_id2sha256(std::string_view device_id);
SYNCSPIRIT_TEST_API model::device_ptr_t make_device(std::string_view device_id, std::string_view name = "");
SYNCSPIRIT_TEST_API std::string hash_string(const std::string_view &hash) noexcept;
SYNCSPIRIT_TEST_API bool has_ipv6() noexcept;

} // namespace syncspirit::test
