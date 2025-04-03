// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "folder_entity.h"
#include "folder_presence.h"
#include "file_entity.h"
#include "model/folder.h"
#include "model/file_info.h"

#include <boost/nowide/convert.hpp>
#include <filesystem>
#include <map>

using namespace syncspirit;
using namespace syncspirit::presentation;

namespace bfs = std::filesystem;

struct path_t {
    using components_t = std::vector<std::string>;
    components_t components;
    std::string parent;
    std::string self;
};

using file_entity_ptr_t = model::intrusive_ptr_t<file_entity_t>;

struct visit_info_t {
    file_entity_ptr_t file;
    path_t path;
};

using new_files_t = std::map<std::string_view, visit_info_t>;
using file_entities_t = std::unordered_map<std::string_view, file_entity_ptr_t>;

static path_t decompose(std::string_view name) {
    auto file_path = bfs::path(boost::nowide::widen(name));
    auto path = path_t();
    path.self = name;
    for (auto it = file_path.begin(); it != file_path.end(); ++it) {
        auto name = boost::nowide::narrow(it->wstring());
        path.components.emplace_back(std::move(name));
    }

    if (path.components.size() > 1) {
        auto parent = file_path.parent_path();
        path.parent = boost::nowide::narrow(parent.wstring());
    }
    return path;
}

static void process(model::folder_info_t *folder_info, entity_t::children_t &files, new_files_t &new_files) {
    auto &files_map = folder_info->get_file_infos();
    for (auto &it_file : files_map) {
        auto &file = *it_file.item;
        auto name = file.get_name();
        if (new_files.count(name)) {
            continue;
        }
        auto path = decompose(name);
        auto &own_name = path.components.back();
        auto child = file_entity_ptr_t(new file_entity_t(file, own_name));
        auto info = visit_info_t{
            std::move(child),
            std::move(path),
        };
        new_files.emplace(name, std::move(info));
    }
}

static void process(new_files_t &new_files, folder_entity_t *self) {
    auto file_entities = file_entities_t();
    for (auto &it : new_files) {
        auto &path = it.second.path;
        auto &entity = it.second.file;
        auto parent = (entity_t *)(nullptr);
        if (path.components.size() == 1) {
            parent = self;

        } else {
            auto it_parent = file_entities.find(it.second.path.parent);
            if (it_parent != file_entities.end()) {
                parent = it_parent->second.get();
            } else {
                std::abort();
            }
        }
        if (parent) {
            parent->add_child(entity);
        }
        file_entities[path.self] = entity;
    }
}

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
    auto &fi_map = folder.get_folder_infos();
    auto new_files = new_files_t();
    for (auto &it_fi : fi_map) {
        process(it_fi.item.get(), children, new_files);
    }
    process(new_files, this);
}

void folder_entity_t::on_insert(model::folder_info_t &folder_info) {
    auto device = folder_info.get_device();
    for (auto &r : records) {
        if (r.device == device) {
            return;
        }
    }
    auto p = new folder_presence_t(*this, folder_info);
    records.emplace_back(record_t{folder_info.get_device(), p});

    auto new_files = new_files_t();
    process(&folder_info, children, new_files);
    process(new_files, this);
}

void folder_entity_t::on_insert(model::file_info_t &file_info) {
    auto path = decompose(file_info.get_name());
    auto &components = path.components;
    auto &own_name = components.back();
    auto child = file_entity_ptr_t(new file_entity_t(file_info, own_name));

    auto parent = static_cast<entity_t *>(this);
    if (components.size() > 1) {
        for (size_t i = 0; i < components.size() - 1; ++i) {
            auto &parent_name = components[i];
            auto &children = parent->children;
            auto it = children.equal_range(parent_name).first;
            if (it != children.end()) {
                parent = it->get();
            } else {
                std::abort();
            }
        }
    }
    parent->add_child(std::move(child));
}

auto folder_entity_t::get_folder() -> model::folder_t & { return folder; }
