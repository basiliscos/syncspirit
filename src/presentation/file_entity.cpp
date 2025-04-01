// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "file_entity.h"
#include "model/folder.h"
#include "model/folder_info.h"

#include <boost/container/small_vector.hpp>

using namespace syncspirit;
using namespace syncspirit::presentation;

static constexpr size_t FILES_ON_STACK = 16;

using presensce_files_t = boost::container::small_vector<model::file_info_t *, FILES_ON_STACK>;

file_entity_t::file_entity_t(model::file_info_t &sample_file, std::string_view own_name_)
    : entity_t(nullptr), own_name{own_name_} {
    auto presensce_files = presensce_files_t();
    auto name = sample_file.get_name();
    auto &fi_map = sample_file.get_folder_info()->get_folder()->get_folder_infos();
    for (auto &it_fi : fi_map) {
        auto &folder_info = it_fi.item;
        auto &files_map = folder_info->get_file_infos();
        auto file = files_map.by_name(name);
        if (file) {
            presensce_files.emplace_back(file.get());
        }
    }
}
