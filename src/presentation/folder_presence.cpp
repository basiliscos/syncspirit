// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "folder_presence.h"
#include "folder_entity.h"
#include "model/cluster.h"

using namespace syncspirit::presentation;

folder_presence_t::folder_presence_t(folder_entity_t &entity_, model::folder_info_t &fi) noexcept
    : presence_t(&entity_, fi.get_device()), folder_info{fi} {
    link(&fi);
    features = features_t::folder;
    auto local = fi.get_device() == entity_.get_folder().get_cluster()->get_device();
    features |= (local ? features_t::local : features_t::peer);
}

auto folder_presence_t::get_folder_info() noexcept -> model::folder_info_t & { return folder_info; }
auto folder_presence_t::get_folder_info() const noexcept -> const model::folder_info_t & { return folder_info; }

auto folder_presence_t::get_link(std::string_view name, bool is_dir) const noexcept -> presence_link_t {
    auto parent = static_cast<presence_t *>(const_cast<folder_presence_t *>(this));
    auto path = name;
    while (!path.empty() && parent) {
        auto index = path.find("/");
        if (index == std::string_view::npos) {
            break;
        } else {
            auto subdir_name = path.substr(0, index - 1);
            path = path.substr(index + 1);
            parent = presentation::get_child(parent, subdir_name, true);
        }
    }

    auto child = (presence_t *)(nullptr);
    if (parent) {
        child = presentation::get_child(parent, path, is_dir);
    }
    return {parent, child};
}
