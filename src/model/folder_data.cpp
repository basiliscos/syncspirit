// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "folder_data.h"
#include "proto/proto-helpers.h"

#include <boost/nowide/convert.hpp>

using namespace syncspirit::model;

void folder_data_t::assign_fields(const db::Folder &item) noexcept {
    id = db::get_id(item);
    label = db::get_label(item);
    path = boost::nowide::widen(db::get_path(item));
    folder_type =  db::get_folder_type(item);
    rescan_interval = db::get_rescan_interval(item);
    pull_order = db::get_pull_order(item);
    scheduled = db::get_scheduled(item);
    read_only = db::get_read_only(item);
    ignore_permissions = db::get_ignore_permissions(item);
    ignore_delete = db::get_ignore_delete(item);
    disable_temp_indixes = db::get_disable_temp_indexes(item);
    paused = db::get_paused(item);
}

void folder_data_t::serialize(db::Folder &r) const noexcept {
    db::set_id(r, id);
    db::set_label(r, label);
    db::set_read_only(r, read_only);
    db::set_ignore_permissions(r, ignore_permissions);
    db::set_ignore_delete(r, ignore_delete);
    db::set_disable_temp_indexes(r, disable_temp_indixes);
    db::set_paused(r, paused);
    db::set_scheduled(r, scheduled);
    db::set_path(r, path.string());
    db::set_folder_type(r, folder_type);
    db::set_pull_order(r, pull_order);
    db::set_rescan_interval(r, rescan_interval);
}
