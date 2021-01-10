#include "cluster.h"

using namespace syncspirit::model;

void cluster_t::add_folder(const folder_ptr_t &folder) noexcept { folders.emplace(folder->id, folder); }
