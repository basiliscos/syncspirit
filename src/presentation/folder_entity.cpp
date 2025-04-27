// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "folder_entity.h"
#include "folder_presence.h"
#include "file_presence.h"
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

folder_entity_t::folder_entity_t(model::folder_ptr_t folder_) noexcept : entity_t({}), folder(*folder_.get()) {
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
    commit(path);
}

void folder_entity_t::on_insert(model::folder_info_t &folder_info) noexcept {
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

entity_t *folder_entity_t::on_insert(model::file_info_t &file_info) noexcept {
    auto path = path_t(file_info.get_name());

    auto entity = static_cast<entity_t *>(this);
    size_t i = 0;
    for (auto piece : path) {
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
    bool has_parent = i + 1 == path.get_pieces_size();
    auto diff = entity_stats_t{};
    if (has_parent) {
        child.reset(new file_entity_t(file_info, std::move(path)));
        entity->add_child(*child);
        orphans.reap_children(child);
        child->commit(child->get_path());
        diff = child->get_stats();
        auto device = file_info.get_folder_info()->get_device();
        entity->push_stats({diff, 0}, device, true);
        for (auto &[d, presence, cp] : records) {
            cp.clear();
        }
        return child.get();
    } else if (i == path.get_pieces_size()) {
        auto device = file_info.get_folder_info()->get_device();
        auto presence_diff = presence_stats_t{};
        auto entry_diff = entity_stats_t{};
        for (auto &[d, presence, cp] : records) {
            if (d == device) {
                presence_diff = -presence->get_own_stats();
                cp.clear();
            }
            if (d == best_device) {
                entry_diff = -presence->get_stats();
            }
        }
        auto file_entity = static_cast<file_entity_t *>(entity);
        auto [file_presence, file_diff] = file_entity->on_insert(file_info);
        presence_diff += file_diff;
        if (auto parent = entity->parent; parent) {
            parent->push_stats(presence_diff, device, false);
        }
        auto prev_best = best_device;
        auto best_presence = entity->recalc_best();
        if (best_device != prev_best) {
            assert(best_presence == file_presence);
            entry_diff += best_presence->get_stats();
            entity->push_stats({entry_diff, 0}, nullptr, true);
        }
        return entity;
    } else {
        auto orphan = orphans.get_by_path(path.get_full_name());
        if (orphan) {
            static_cast<file_entity_t *>(orphan.get())->on_insert(file_info);
        } else {
            child.reset(new file_entity_t(file_info, std::move(path)));
            orphans.push(child);
        }
    }
    return {};
}

auto folder_entity_t::get_folder() noexcept -> model::folder_t & { return folder; }
