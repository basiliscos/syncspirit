// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "folder_presence.h"
#include "folder_entity.h"
#include "model/folder_info.h"

using namespace syncspirit::presentation;

folder_presence_t::folder_presence_t(folder_entity_t &entity_, model::folder_info_t &fi)
    : presence_t(&entity_, fi.get_device()), folder_info{fi} {
    link(&fi);
    features = features_t::folder;
}

auto folder_presence_t::get_folder_info() -> model::folder_info_t & { return folder_info; }
