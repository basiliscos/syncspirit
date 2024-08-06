// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "update_folder_info.h"
#include "model/diff/cluster_visitor.h"
#include "model/cluster.h"
#include "model/misc/error_code.h"
#include "utils/format.hpp"

using namespace syncspirit::model::diff::modify;

update_folder_info_t::update_folder_info_t(db::FolderInfo db, const device_t &device, const folder_t &folder) noexcept
    : item(std::move(db)), device_id{device.device_id().get_sha256()}, folder_id{folder.get_id()} {}

auto update_folder_info_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto device = cluster.get_devices().by_sha256(device_id);
    if (!device) {
        return make_error_code(error_code_t::no_such_device);
    }
    auto folder = cluster.get_folders().by_id(folder_id);
    auto &folder_infos = folder->get_folder_infos();
    auto fi = folder_infos.by_device(*device);
    if (fi) {
        fi->set_max_sequence(item.max_sequence());
        LOG_TRACE(log, "applyging update_folder_info_t (update), device {}, folder = {}", device->device_id(),
                  folder->get_label());
    } else {
        auto fi_opt = folder_info_t::create(cluster.next_uuid(), item, device, folder);
        if (!fi_opt) {
            return fi_opt.assume_error();
        }

        auto &fi = fi_opt.value();
        folder->add(fi);

        LOG_TRACE(log, "applyging update_folder_info_t (new/reset), device {}, folder = {}", device->device_id(),
                  folder->get_label());
    }
    return applicator_t::apply_sibling(cluster);
}

auto update_folder_info_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting update_folder_info_t");
    return visitor(*this, custom);
}
