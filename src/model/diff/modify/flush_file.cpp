// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "flush_file.h"

#include "../cluster_visitor.h"
#include "../../cluster.h"

using namespace syncspirit::model::diff::modify;

flush_file_t::flush_file_t(const model::file_info_t &file) noexcept {
    auto fi = file.get_folder_info();
    auto folder = fi->get_folder();
    folder_id = folder->get_id();
    device_id = fi->get_device()->device_id().get_sha256();
    file_name = file.get_name();
}

auto flush_file_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    assert(cluster.get_folders()
               .by_id(folder_id)
               ->get_folder_infos()
               .by_device_id(device_id)
               ->get_file_infos()
               .by_name(file_name)
               ->is_locally_available());
    return outcome::success();
}

auto flush_file_t::visit(cluster_visitor_t &visitor) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting flush_file_t, folder = {}, file = {}", folder_id, file_name);
    return visitor(*this);
}
