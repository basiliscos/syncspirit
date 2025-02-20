// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "folder_data.h"
#include "proto/proto-structs.h"

#include <boost/nowide/convert.hpp>

using namespace syncspirit::model;

void folder_data_t::assign_fields(const db::Folder &item) noexcept {
    id = item.id();
    label = item.label();
    path = boost::nowide::widen(item.path());
    folder_type = (folder_type_t)item.folder_type();
    rescan_interval = item.rescan_interval();
    pull_order = (pull_order_t)item.pull_order();
    scheduled = item.scheduled();
    read_only = item.read_only();
    ignore_permissions = item.ignore_permissions();
    ignore_delete = item.ignore_delete();
    disable_temp_indixes = item.disable_temp_indexes();
    paused = item.paused();
}

void folder_data_t::serialize(db::Folder &r) const noexcept {
    r.id(id);
    r.label(label);
    r.read_only(read_only);
    r.ignore_permissions(ignore_permissions);
    r.ignore_delete(ignore_delete);
    r.disable_temp_indexes(disable_temp_indixes);
    r.paused(paused);
    r.scheduled(scheduled);
    r.path(path.string());
    r.folder_type((db::FolderType)folder_type);
    r.pull_order((db::PullOrder)pull_order);
    r.rescan_interval(rescan_interval);
}
