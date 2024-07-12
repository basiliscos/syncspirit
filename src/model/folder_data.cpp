// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "folder_data.h"
#include "bep.pb.h"

using namespace syncspirit::model;

void folder_data_t::assign_fields(const db::Folder &item) noexcept {
    id = item.id();
    label = item.label();
    path = item.path();
    folder_type = (folder_type_t)item.folder_type();
    rescan_interval = item.rescan_interval();
    pull_order = (pull_order_t)item.pull_order();
    watched = item.watched();
    read_only = item.read_only();
    ignore_permissions = item.ignore_permissions();
    ignore_delete = item.ignore_delete();
    disable_temp_indixes = item.disable_temp_indexes();
    paused = item.paused();
}

void folder_data_t::serialize(db::Folder &r) const noexcept {
    r.set_id(id);
    r.set_label(label);
    r.set_read_only(read_only);
    r.set_ignore_permissions(ignore_permissions);
    r.set_ignore_delete(ignore_delete);
    r.set_disable_temp_indexes(disable_temp_indixes);
    r.set_paused(paused);
    r.set_watched(watched);
    r.set_path(path.string());
    r.set_folder_type((db::FolderType)folder_type);
    r.set_pull_order((db::PullOrder)pull_order);
    r.set_rescan_interval(rescan_interval);
}
