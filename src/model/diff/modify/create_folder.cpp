// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "create_folder.h"
#include "../cluster_visitor.h"
#include "../../cluster.h"
#include "../../misc/error_code.h"

using namespace syncspirit::model::diff::modify;

auto create_folder_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "applyging create_folder_t, folder_id: {}", item.id());
    auto &folders = cluster.get_folders();
    auto prev_folder = folders.by_id(item.id());
    if (prev_folder) {
        return make_error_code(error_code_t::folder_already_exists);
    }

    auto uuid = cluster.next_uuid();
    auto folder_opt = folder_t::create(uuid, item);
    if (!folder_opt) {
        return folder_opt.assume_error();
    }
    auto &folder = folder_opt.value();

    auto &my_device = cluster.get_device();
    db::FolderInfo db_fi_my;
    db_fi_my.set_index_id(cluster.next_uint64());

    auto fi_my_opt = folder_info_t::create(cluster.next_uuid(), db_fi_my, my_device, folder);
    if (!fi_my_opt) {
        return fi_my_opt.assume_error();
    }
    auto &fi_my = fi_my_opt.value();

    auto &folder_infos = folder->get_folder_infos();
    folder_infos.put(fi_my);
    folders.put(folder);
    folder->assign_cluster(&cluster);

    return outcome::success();
}

auto create_folder_t::visit(cluster_visitor_t &visitor) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting create_folder_t");
    return visitor(*this);
}
