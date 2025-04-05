// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "folder_entity.h"
#include "folder_presence.h"
#include "file_entity.h"
#include "model/folder.h"
#include "model/file_info.h"

#include <boost/nowide/convert.hpp>
#include <map>

using namespace syncspirit;
using namespace syncspirit::presentation;

namespace bfs = std::filesystem;

using file_entity_ptr_t = model::intrusive_ptr_t<file_entity_t>;
using new_files_t = std::map<std::string_view, file_entity_ptr_t>;
using file_entities_t = std::unordered_map<std::string_view, file_entity_ptr_t>;

static void process(model::folder_info_t *folder_info, entity_t::children_t &files, new_files_t &new_files) {
    auto &files_map = folder_info->get_file_infos();
    for (auto &it_file : files_map) {
        auto &file = *it_file.item;
        auto name = file.get_name();
        if (new_files.count(name)) {
            continue;
        }
        auto path = path_t(name);
        auto child = file_entity_ptr_t(new file_entity_t(file, std::move(path)));
        new_files.emplace(name, std::move(child));
    }
}

static void process_files(new_files_t &new_files, orphans_t &orphans, folder_entity_t *self) {
    auto file_entities = file_entities_t();
    for (auto &it : new_files) {
        auto &entity = it.second;
        auto &path = entity->get_path();
        auto parent = (entity_t *)(nullptr);
        if (path.get_pieces_size() == 1) {
            parent = self;

        } else {
            auto parent_path = path.get_parent_name();
            auto it_parent = file_entities.find(parent_path);
            if (it_parent != file_entities.end()) {
                parent = it_parent->second.get();
            } else {
                orphans.push(entity);
            }
        }
        if (parent) {
            parent->add_child(*entity);
        }
        file_entities[path.get_full_name()] = entity;
    }
}

folder_entity_t::folder_entity_t(model::folder_ptr_t folder_) : entity_t({}), folder(*folder_.get()) {
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
    process_files(new_files, orphans, this);
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
    process_files(new_files, orphans, this);
}

void folder_entity_t::on_insert(model::file_info_t &file_info) {
    auto path = path_t(file_info.get_name());

    auto entity = static_cast<entity_t *>(this);
    size_t i = 0;
    for (auto piece : path) {
        auto &children = entity->children;
        auto it = children.equal_range(piece).first;
        if (it != children.end()) {
            entity = *it;
        } else {
            break;
        }
        ++i;
    }

    auto child = file_entity_ptr_t();
    bool has_parent = i + 1 == path.get_pieces_size();
    if (has_parent) {
        child.reset(new file_entity_t(file_info, std::move(path)));
        entity->add_child(*child);
    } else if (i == path.get_pieces_size()) {
        static_cast<file_entity_t *>(entity)->on_insert(file_info);
    } else {
        child.reset(new file_entity_t(file_info, std::move(path)));
        orphans.push(child);
    }
    if (child && has_parent) {
        orphans.reap_children(child);
    }
}

auto folder_entity_t::get_folder() -> model::folder_t & { return folder; }
