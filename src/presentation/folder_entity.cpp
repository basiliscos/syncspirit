// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "folder_entity.h"
#include "folder_presence.h"
#include "model/folder.h"

using namespace syncspirit::presentation;

folder_entity_t::folder_entity_t(model::folder_ptr_t folder_) : entity_t(nullptr), folder(*folder_.get()) {
    folder.set_augmentation(this);
    auto &folders_map = folder.get_folder_infos();
    records.reserve(folders_map.size());
    for (auto &it : folders_map) {
        auto &folder_info = *it.item;
        auto p = new folder_presence_t(*this, folder_info);
        records.emplace_back(record_t{folder_info.get_device(), p});
    }
}

auto folder_entity_t::get_folder() -> model::folder_t & { return folder; }
