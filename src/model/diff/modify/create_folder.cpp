// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "create_folder.h"
#include "../cluster_visitor.h"
#include "../../cluster.h"
#include "../../misc/error_code.h"
#include "upsert_folder_info.h"

using namespace syncspirit::model::diff::modify;

auto create_folder_t::create(const cluster_t &cluster, sequencer_t &sequencer, db::Folder db) noexcept
    -> outcome::result<cluster_diff_ptr_t> {
    auto &folders = cluster.get_folders();
    auto prev_folder = folders.by_id(db.id());
    if (prev_folder) {
        return make_error_code(error_code_t::folder_already_exists);
    }

    auto diff = cluster_diff_ptr_t{};
    diff = new create_folder_t(sequencer, std::move(db), *cluster.get_device());
    return outcome::success(diff);
}

create_folder_t::create_folder_t(sequencer_t &sequencer, db::Folder db_, const model::device_t &device) noexcept
    : db{std::move(db_)} {

    uuid = sequencer.next_uuid();
    auto fi_uuid = sequencer.next_uuid();
    auto fi_index = sequencer.next_uint64();
    auto diff = cluster_diff_ptr_t{};

    diff = new upsert_folder_info_t(fi_uuid, device.device_id().get_sha256(), db.id(), fi_index, 0);
    assign_child(diff);
}

auto create_folder_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "applying create_folder_t, folder_id: {}", db.id());
    auto folder_opt = folder_t::create(uuid, db);
    if (!folder_opt) {
        return folder_opt.assume_error();
    }
    auto &folders = cluster.get_folders();
    auto &folder = folder_opt.value();
    folder->assign_cluster(&cluster);
    folders.put(folder);

    auto r = applicator_t::apply_child(cluster);
    if (!r) {
        return r;
    }

    return applicator_t::apply_sibling(cluster);
}

auto create_folder_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting create_folder_t");
    return visitor(*this, custom);
}
