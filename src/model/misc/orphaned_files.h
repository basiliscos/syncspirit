// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "utils/path.h"
#include "utils/path_cache.h"
#include "proto/proto-fwd.hpp"
#include <map>
#include <vector>

namespace syncspirit::model {

struct SYNCSPIRIT_API orphaned_files_t {
    struct comparator_t {
        using is_transparent = void;

        bool operator()(const utils::path_ptr_t& l, const utils::path_ptr_t& r) const {
            return *l < *r;
        }
        bool operator()(const utils::path_base_t& l, const utils::path_ptr_t& r) const {
            return l < *r;
        }
        bool operator()(const utils::path_ptr_t& l, const utils::path_base_t& r) const {
            return *l < r;
        }
    };

    using file_t = proto::FileInfo;
    using files_t = std::vector<file_t>;
    using container_t = std::map<utils::path_ptr_t, files_t, comparator_t>;

    orphaned_files_t() noexcept = default;
    orphaned_files_t(const orphaned_files_t&) = delete;
    orphaned_files_t(orphaned_files_t&&) noexcept = default;

    orphaned_files_t& operator +=(orphaned_files_t) noexcept;
    void record(const utils::poly_path_view_t&, utils::path_cache_t&, file_t) noexcept;
    files_t commit(utils::path_t& parent_path) noexcept;

    container_t container;
};

} // namespace syncspirit::model
