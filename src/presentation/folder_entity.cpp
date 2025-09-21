// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "folder_entity.h"
#include "folder_presence.h"
#include "file_entity.h"
#include "model/folder.h"
#include "model/file_info.h"
#include "model/cluster.h"
#include <unordered_map>
#include <vector>

using namespace syncspirit;
using namespace syncspirit::presentation;

namespace bfs = std::filesystem;

using file_entity_ptr_t = model::intrusive_ptr_t<file_entity_t>;
using file_entities_t = std::unordered_map<model::path_t *, file_entity_ptr_t>;

static void process(model::folder_info_t *folder_info, entity_t::children_t &files, file_entities_t &new_files) {
    auto &files_map = folder_info->get_file_infos();
    new_files.reserve(files_map.size());
    for (auto &it_file : files_map) {
        auto &file = *it_file;
        auto &name = file.get_name();
        if (new_files.count(name.get())) {
            continue;
        }
        auto& folders_map = folder_info->get_folder()->get_folder_infos();
        auto child = file_entity_ptr_t(new file_entity_t(name, folders_map));
        new_files.emplace(name.get(), std::move(child));
    }
}

static void process_files(file_entities_t &&new_files, orphans_t &orphans, folder_entity_t *self) {
    using entities_vector_t = std::vector<file_entity_ptr_t>;
    auto entities = entities_vector_t();
    entities.reserve(new_files.size());
    for (auto &[_, file_entry] : new_files) {
        entities.emplace_back((std::move(file_entry)));
    }
    auto comparator = [](const file_entity_ptr_t &lhs, const file_entity_ptr_t &rhs) {
        return lhs->get_path()->get_full_name() < rhs->get_path()->get_full_name();
    };
    std::sort(entities.begin(), entities.end(), comparator);

    auto &path_cache = self->get_folder().get_cluster()->get_path_cache();
    auto file_entities = file_entities_t();
    for (auto &entity : entities) {
        auto &path = entity->get_path();
        auto parent = (entity_t *)(nullptr);
        if (path->get_pieces_size() == 1) {
            parent = self;

        } else {
            auto parent_name = path->get_parent_name();
            auto parent_path = path_cache.get_path(parent_name);
            auto it_parent = file_entities.find(parent_path.get());
            if (it_parent != file_entities.end()) {
                parent = it_parent->second.get();
            } else {
                orphans.push(entity);
            }
        }
        if (parent) {
            parent->add_child(*entity);
        }
        file_entities[path.get()] = entity;
    }
}

folder_entity_t::folder_entity_t(model::folder_ptr_t folder_) noexcept
    : entity_t(new model::path_t()), folder(*folder_.get()) {
    folder.set_augmentation(this);

    // make folder_infos as presence
    auto &folders_map = folder.get_folder_infos();
    presences.reserve(folders_map.size());
    for (auto &it : folders_map) {
        auto &folder_info = *it.item;
        auto p = new folder_presence_t(*this, folder_info);
        presences.emplace_back(p);
    }

    // make all-files as chilren, make hierarchy
    auto new_files = file_entities_t();
    for (auto &it : folders_map) {
        process(it.item.get(), children, new_files);
    }
    process_files(std::move(new_files), orphans, this);
    commit(*path, nullptr);
}

auto folder_entity_t::on_insert(model::folder_info_t &folder_info) noexcept -> folder_presence_t * {
    auto device = folder_info.get_device();
    for (auto p : presences) {
        if (p->get_device() == device) {
            return {};
        }
    }
    auto p = new folder_presence_t(*this, folder_info);
    presences.emplace_back(p);

    auto new_files = file_entities_t();
    process(&folder_info, children, new_files);
    process_files(std::move(new_files), orphans, this);
    commit(*path, device);
    return p;
}

entity_t *folder_entity_t::on_insert(model::file_info_t &file_info, const model::folder_info_t& folder_info) noexcept {
    auto &path = file_info.get_name();
    auto device = folder_info.get_device();

    auto entity = static_cast<entity_t *>(this);
    size_t i = 0;
    for (auto piece : *path) {
        auto &children = entity->children;
        auto it = children.find(piece);
        if (it != children.end()) {
            entity = *it;
        } else {
            break;
        }
        ++i;
    }

    auto child = file_entity_ptr_t();
    bool has_parent = i + 1 == path->get_pieces_size();
    auto diff = entity_stats_t{};
    if (has_parent) {
        child.reset(new file_entity_t(path, folder_info.get_folder()->get_folder_infos()));
        entity->add_child(*child);
        orphans.reap_children(child);
        child->commit(*child->get_path(), device);
        recalc_best();
        return child.get();
    } else if (i == path->get_pieces_size()) {
        auto file_entity = static_cast<file_entity_t *>(entity);
        auto file_presence = file_entity->on_insert(file_info, folder_info);
        entity->commit(*entity->get_path(), device);
        if (auto monitor = get_monitor(); monitor) {
            monitor->on_update(*entity);
        }
        return entity;
    } else {
        auto orphan = orphans.get_by_path(path.get());
        if (orphan) {
            static_cast<file_entity_t *>(orphan.get())->on_insert(file_info, folder_info);
        } else {
            child.reset(new file_entity_t(path, folder_info.get_folder()->get_folder_infos()));
            orphans.push(child);
        }
    }
    return {};
}

auto folder_entity_t::get_folder() noexcept -> model::folder_t & { return folder; }

void folder_entity_t::on_update() noexcept { notify_update(); }

void folder_entity_t::on_delete() noexcept {
    clear_children();
    if (auto monitor = get_monitor(); monitor) {
        monitor->on_delete(*this);
    }
}
