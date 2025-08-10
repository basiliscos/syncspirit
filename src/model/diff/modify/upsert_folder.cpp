// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "upsert_folder.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"
#include "model/misc/file_iterator.h"
#include "upsert_folder_info.h"
#include "proto/proto-helpers-db.h"
#include "utils/format.hpp"

using namespace syncspirit::model::diff::modify;

auto upsert_folder_t::create(const cluster_t &cluster, sequencer_t &sequencer, db::Folder db,
                             std::uint64_t index_id) noexcept -> outcome::result<cluster_diff_ptr_t> {
    auto &folders = cluster.get_folders();
    auto &device = *cluster.get_device();
    auto prev_folder = folders.by_id(db::get_id(db));
    auto folder_info = prev_folder ? prev_folder->get_folder_infos().by_device(device) : nullptr;
    auto uuid = bu::uuid{};
    if (prev_folder) {
        assign(uuid, prev_folder->get_uuid());
    } else {
        uuid = sequencer.next_uuid();
    }

    if (!index_id) {
        index_id = sequencer.next_uint64();
    }

    auto diff = cluster_diff_ptr_t{};
    diff = new upsert_folder_t(sequencer, uuid, std::move(db), folder_info, device.device_id(), index_id);
    return outcome::success(diff);
}

upsert_folder_t::upsert_folder_t(sequencer_t &sequencer, bu::uuid uuid_, db::Folder db_,
                                 model::folder_info_ptr_t folder_info, const device_id_t &device,
                                 std::uint64_t index_id) noexcept
    : uuid{uuid_}, db{std::move(db_)} {
    auto folder_id = db::get_id(db);
    db::set_disable_temp_indexes(db, true); // hard-code, ss does not support for now
    LOG_DEBUG(log, "upsert_folder_t, folder_id = {}, device = {}", folder_id, device);

    if (!folder_info) {
        auto fi_uuid = sequencer.next_uuid();
        auto diff = cluster_diff_ptr_t{};
        diff = new upsert_folder_info_t(fi_uuid, device, device, folder_id, index_id);
        assign_child(diff);
    }
}

auto upsert_folder_t::apply_impl(cluster_t &cluster, apply_controller_t &controller, void *custom) const noexcept
    -> outcome::result<void> {
    auto folder_id = db::get_id(db);
    LOG_TRACE(log, "applying upsert_folder_t, folder_id: {}", folder_id);
    auto folder_opt = folder_t::create(uuid, db);
    if (!folder_opt) {
        return folder_opt.assume_error();
    }
    auto &folder = folder_opt.value();

    auto &folders = cluster.get_folders();
    auto prev_folder = folders.by_id(folder_id);
    if (!prev_folder) {
        folder->assign_cluster(&cluster);
        folders.put(folder);
    } else {
        prev_folder->assign_fields(db);
        if (auto aug = prev_folder->get_augmentation(); aug) {
            aug->on_update();
        }
    }

    auto r = applicator_t::apply_child(cluster, controller, custom);
    if (!r) {
        return r;
    }

    if (prev_folder) {
        for (auto it : cluster.get_devices()) {
            auto &device = *it.item;
            if (&device != cluster.get_device().get()) {
                if (prev_folder->is_shared_with(device)) {
                    if (auto it = device.get_iterator(); it) {
                        it->on_upsert(*prev_folder);
                    }
                }
            }
        }
    }

    return applicator_t::apply_sibling(cluster, controller, custom);
}

auto upsert_folder_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting upsert_folder_t");
    return visitor(*this, custom);
}
