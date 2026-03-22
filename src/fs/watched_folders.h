// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <boost/smart_ptr/local_shared_ptr.hpp>
#include "utils/string_comparator.hpp"

namespace syncspirit::fs {

namespace bfs = std::filesystem;

struct folder_info_t {
    bfs::path path;
    std::string path_str;
};

using watched_folders_t = std::unordered_map<std::string, folder_info_t, utils::string_hash_t, utils::string_eq_t>;
using watched_folders_ptr_t = boost::local_shared_ptr<watched_folders_t>;

} // namespace syncspirit::fs
