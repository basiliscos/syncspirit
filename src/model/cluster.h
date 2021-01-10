#pragma once

#include "folder.h"
#include "arc.hpp"
#include <unordered_map>

namespace syncspirit::model {

struct cluster_t : arc_base_t<cluster_t> {
    using folders_t = std::unordered_map<std::string, folder_ptr_t>;

    void add_folder(const folder_ptr_t &folder) noexcept;

    folders_t folders;
};

using cluster_ptr_t = intrusive_ptr_t<cluster_t>;

} // namespace syncspirit::model
