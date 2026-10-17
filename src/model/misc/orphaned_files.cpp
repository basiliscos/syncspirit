// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "orphaned_files.h"
#include "utils/path_view.hpp"

namespace syncspirit::model {

void orphaned_files_t::record(const utils::poly_path_view_t& path, utils::path_cache_t& cache, file_t file) noexcept {
    auto parent = path.get_parent();
    auto it = container.find(parent);
    if (it == container.end()) {
        auto path_ptr = cache.get_path(parent.get_full_name());
        it = container.emplace(std::move(path_ptr), files_t{}).first;
    }
    it->second.emplace_back(std::move(file));
}

}