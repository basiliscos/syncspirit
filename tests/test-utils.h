// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <boost/filesystem.hpp>
#include "model/device.h"
#include "model/diff/cluster_diff.h"

namespace syncspirit::test {

namespace bfs = boost::filesystem;
namespace sys = boost::system;

struct path_guard_t {
    bfs::path &path;
    path_guard_t(bfs::path &path_) : path{path_} {}
    ~path_guard_t() {
        if (!getenv("SYNCSPIRIT_TEST_KEEP_PATH")) {
            sys::error_code ec;
            bfs::remove_all(path, ec);
            if (ec) {
                printf("error removing %s : %s\n", path.string().c_str(), ec.message().c_str());
            }
        }
    }
};

bfs::path locate_path(const char *test_file);
std::string read_file(const bfs::path &path);
void write_file(const bfs::path &path, std::string_view content);
std::string device_id2sha256(std::string_view device_id);
model::device_ptr_t make_device(std::string_view device_id, std::string_view name = "");
std::string hash_string(const std::string_view &hash) noexcept;


} // namespace syncspirit::test
