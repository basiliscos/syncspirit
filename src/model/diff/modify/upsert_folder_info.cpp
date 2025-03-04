// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "upsert_folder_info.h"

#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"
#include "model/misc/error_code.h"
#include "model/misc/file_iterator.h"
#include "utils/format.hpp"

using namespace syncspirit::model::diff::modify;

upsert_folder_info_t::upsert_folder_info_t(const bu::uuid &uuid_, std::string_view device_id_,
                                           std::string_view folder_id_, std::uint64_t index_id_) noexcept
    : uuid{uuid_}, device_id{device_id_}, folder_id{folder_id_}, index_id{index_id_} {
    LOG_DEBUG(log, "upsert_folder_info_t, folder = {}, index = {}", folder_id, index_id);
}

auto upsert_folder_info_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept
    -> outcome::result<void> {
    auto device = cluster.get_devices().by_sha256(device_id);
    if (!device) {
        return make_error_code(error_code_t::no_such_device);
    }

    auto folder = cluster.get_folders().by_id(folder_id);
    if (!folder) {
        return make_error_code(error_code_t::no_such_folder);
    }
    auto &folder_infos = folder->get_folder_infos();
    auto fi = folder_infos.by_device(*device);
    if (fi) {
        fi->set_index(index_id);
        LOG_TRACE(log, "applying upsert_folder_info_t (update), folder = {} ({}), device = {}, index = {:x}",
                  folder->get_label(), folder_id, device->device_id(), index_id);
        fi->notify_update();
    } else {
        LOG_TRACE(log, "applying upsert_folder_info_t (create), folder = {} ({}), device = {}, index = {:x}",
                  folder->get_label(), folder_id, device->device_id(), index_id);
        db::FolderInfo db;
        db.set_index_id(index_id);

        auto opt = folder_info_t::create(uuid, db, device, folder);
        if (!opt) {
            return opt.assume_error();
        }
        fi = std::move(opt.value());
        folder_infos.put(fi);
    }

    if (auto iterator = fi->get_device()->get_iterator(); iterator) {
        iterator->on_upsert(fi);
    }

    return applicator_t::apply_sibling(cluster, controller);
}

auto upsert_folder_info_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting upsert_folder_info_t");
    return visitor(*this, custom);
}
