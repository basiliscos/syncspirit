// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "finish_file.h"

#include "../cluster_visitor.h"
#include "model/device_id.h"
#include "model/cluster.h"
#include "utils/format.hpp"

using namespace syncspirit::model::diff::modify;

finish_file_t::finish_file_t(const model::file_info_t &file) noexcept {
    auto fi = file.get_folder_info();
    auto folder = fi->get_folder();
    auto &device_id = fi->get_device()->device_id();
    folder_id = folder->get_id();
    file_name = file.get_name();
    peer_id = device_id.get_sha256();
    assert(device_id != folder->get_cluster()->get_device()->device_id());
    LOG_DEBUG(log, "finish_file_t, file = {}, folder = {}, peer = {}", file_name, folder_id, device_id);
}

auto finish_file_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting finish_file_t (visitor = {}), folder = {}, file = {}", (const void *)&visitor, folder_id,
              file_name);
    return visitor(*this, custom);
}
