// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "mark_reachable.h"
#include "model/cluster.h"
#include "model/diff/apply_controller.h"
#include "model/diff/cluster_visitor.h"
#include "utils/format.hpp"

using namespace syncspirit::model::diff::modify;

mark_reachable_t::mark_reachable_t(const model::file_info_t &file, bool reachable_) noexcept
    : folder_id{file.get_folder_info()->get_folder()->get_id()}, file_name{file.get_name()}, reachable{reachable_} {
    auto &peer = file.get_folder_info()->get_device()->device_id();
    device_id = peer.get_sha256();
    LOG_DEBUG(log, "mark_reachable_t, file = {}, folder = {}, device = {}, value = {}", file_name, folder_id, peer,
              reachable);
}

auto mark_reachable_t::apply_impl(apply_controller_t &controller, void *custom) const noexcept
    -> outcome::result<void> {
    auto &cluster = controller.get_cluster();
    auto folder = cluster.get_folders().by_id(folder_id);
    auto device = cluster.get_devices().by_sha256(device_id);
    auto folder_info = folder->get_folder_infos().by_device(*device);
    auto file = folder_info->get_file_infos().by_name(file_name);

    LOG_TRACE(log, "applying reachable ({}) for '{}'", reachable, file->get_full_name());
    file->mark_unreachable(!reachable);

    return applicator_t::apply_sibling(controller, custom);
}

auto mark_reachable_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting mark_reachable_t");
    return visitor(*this, custom);
}
