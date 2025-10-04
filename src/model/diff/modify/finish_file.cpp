// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "finish_file.h"

#include "../cluster_visitor.h"
#include "model/device_id.h"
#include "model/cluster.h"
#include "utils/format.hpp"

using namespace syncspirit::model::diff::modify;

finish_file_t::finish_file_t(const model::file_info_t &file, const folder_info_t &fi) noexcept {
    auto folder = fi.get_folder();
    auto &device_id = fi.get_device()->device_id();
    folder_id = folder->get_id();
    file_name = file.get_name()->get_full_name();
    peer_id = device_id.get_sha256();
    auto local_folder = folder->get_folder_infos().by_device(*folder->get_cluster()->get_device());
    auto local_file = local_folder->get_file_infos().by_name(file_name);
    assert(device_id != folder->get_cluster()->get_device()->device_id());
    action = resolve(file, local_file.get(), *local_folder);
    LOG_DEBUG(log, "finish_file_t, file: '{}', peer = {}, action = {}", file, device_id, (int)action);
    assert(action != advance_action_t::ignore);
}

auto finish_file_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting finish_file_t (visitor = {}), folder = {}, file = {}", (const void *)&visitor, folder_id,
              file_name);
    return visitor(*this, custom);
}
