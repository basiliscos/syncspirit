// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "upsert_folder.h"
#include "model/cluster.h"
#include "model/diff//cluster_visitor.h"
#include "upsert_folder_info.h"

using namespace syncspirit::model::diff::modify;

auto upsert_folder_t::create(const cluster_t &cluster, sequencer_t &sequencer, db::Folder db) noexcept
    -> outcome::result<cluster_diff_ptr_t> {
    auto &folders = cluster.get_folders();
    auto &device = *cluster.get_device();
    auto prev_folder = folders.by_id(db.id());
    auto folder_info = prev_folder ? prev_folder->get_folder_infos().by_device(device) : nullptr;
    auto uuid = uuid_t{};
    if (prev_folder) {
        assign(uuid, prev_folder->get_uuid());
    } else {
        uuid = sequencer.next_uuid();
    }

    auto diff = cluster_diff_ptr_t{};
    diff = new upsert_folder_t(sequencer, uuid, std::move(db), folder_info, device);
    return outcome::success(diff);
}

upsert_folder_t::upsert_folder_t(sequencer_t &sequencer, uuid_t uuid_, db::Folder db_,
                                 model::folder_info_ptr_t folder_info, const model::device_t &device) noexcept
    : db{std::move(db_)}, uuid{uuid_} {

    if (!folder_info) {
        auto fi_uuid = sequencer.next_uuid();
        auto fi_index = sequencer.next_uint64();
        auto diff = cluster_diff_ptr_t{};
        diff = new upsert_folder_info_t(fi_uuid, device.device_id().get_sha256(), db.id(), fi_index, 0);
        assign_child(diff);
    }
}

auto upsert_folder_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "applying upsert_folder_t, folder_id: {}", db.id());
    auto folder_opt = folder_t::create(uuid, db);
    if (!folder_opt) {
        return folder_opt.assume_error();
    }
    auto &folder = folder_opt.value();

    auto &folders = cluster.get_folders();
    auto prev_folder = folders.by_id(db.id());
    if (!prev_folder) {
        folder->assign_cluster(&cluster);
        folders.put(folder);
    } else {
        prev_folder->assign_fields(db);
        if (auto aug = prev_folder->get_augmentation(); aug) {
            aug->on_update();
        }
    }

    auto r = applicator_t::apply_child(cluster);
    if (!r) {
        return r;
    }

    return applicator_t::apply_sibling(cluster);
}

auto upsert_folder_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting upsert_folder_t");
    return visitor(*this, custom);
}
