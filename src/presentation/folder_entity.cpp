// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "folder_entity.h"
#include "folder_presence.h"
#include "file_entity.h"
#include "model/folder.h"
#include "model/file_info.h"

#include <boost/nowide/convert.hpp>
#include <filesystem>
#include <unordered_map>

using namespace syncspirit;
using namespace syncspirit::presentation;

namespace bfs = std::filesystem;

using path_t = std::vector<std::string>;
using file_entity_ptr_t = model::intrusive_ptr_t<file_entity_t>;

struct visit_info_t {
    file_entity_ptr_t file;
    path_t path;
};

folder_entity_t::folder_entity_t(model::folder_ptr_t folder_) : entity_t(nullptr), folder(*folder_.get()) {
    folder.set_augmentation(this);

    // make folder_infos as presence
    auto &folders_map = folder.get_folder_infos();
    records.reserve(folders_map.size());
    for (auto &it : folders_map) {
        auto &folder_info = *it.item;
        auto p = new folder_presence_t(*this, folder_info);
        records.emplace_back(record_t{folder_info.get_device(), p});
    }

    // make all-files as chilren, make hierarchy
    using visited_files_t = std::unordered_map<std::string_view, visit_info_t>;
    auto &fi_map = folder.get_folder_infos();
    auto visited_files = visited_files_t();
    for (auto &it_fi : fi_map) {
        auto &folder_info = it_fi.item;
        auto &files_map = folder_info->get_file_infos();
        for (auto &it_file : files_map) {
            auto &file = *it_file.item;
            auto name = file.get_name();
            if (visited_files.count(name)) {
                continue;
            }
            auto file_path = bfs::path(boost::nowide::widen(name));
            auto string_path = path_t();
            for (auto it = file_path.begin(); it != file_path.end(); ++it) {
                auto name = boost::nowide::narrow(it->wstring());
                string_path.emplace_back(std::move(name));
            }

            auto child = file_entity_ptr_t(new file_entity_t(file, string_path.back()));
            auto info = visit_info_t{
                std::move(child),
                std::move(string_path),
            };
            visited_files.emplace(name, std::move(info));
        }
    }

    for (auto &it : visited_files) {
        children.emplace(std::move(it.second.file));
    }
}

auto folder_entity_t::get_folder() -> model::folder_t & { return folder; }
