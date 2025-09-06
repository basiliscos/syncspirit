// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "lock_file.h"
#include "model/cluster.h"
#include "model/diff/apply_controller.h"
#include "model/diff/cluster_visitor.h"
#include "utils/format.hpp"

using namespace syncspirit::model::diff::modify;

lock_file_t::lock_file_t(const model::file_info_t &file, bool locked_) noexcept
    : folder_id{file.get_folder_info()->get_folder()->get_id()}, file_name{file.get_name()->get_full_name()},
      locked{locked_} {
    auto &peer = file.get_folder_info()->get_device()->device_id();
    device_id = peer.get_sha256();
    LOG_DEBUG(log, "lock_file_t, file: '{}', device = {}, value = {}", file, peer, locked);
}

auto lock_file_t::apply_impl(apply_controller_t &controller, void *custom) const noexcept -> outcome::result<void> {
    auto &cluster = controller.get_cluster();
    auto folder = cluster.get_folders().by_id(folder_id);
    auto d = cluster.get_devices().by_sha256(this->device_id);
    auto folder_info = folder->get_folder_infos().by_device(*d);
    auto file = folder_info->get_file_infos().by_name(file_name);

    if (locked) {
        assert(!file->is_locked());
        file->lock();
    } else {
        assert(file->is_locked());
        file->unlock();
    }
    return applicator_t::apply_sibling(controller, custom);
}

auto lock_file_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting lock_file_t");
    return visitor(*this, custom);
}
