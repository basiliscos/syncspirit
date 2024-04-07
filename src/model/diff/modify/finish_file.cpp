// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "finish_file.h"

#include "../cluster_visitor.h"
#include "../../cluster.h"

using namespace syncspirit::model::diff::modify;

finish_file_t::finish_file_t(const model::file_info_t &file) noexcept {
    assert(file.get_source());
    auto fi = file.get_folder_info();
    auto folder = fi->get_folder();
    folder_id = folder->get_id();
    file_name = file.get_name();
    assert(fi->get_device() == folder->get_cluster()->get_device().get());
}

auto finish_file_t::apply_impl(cluster_t &) const noexcept -> outcome::result<void> { return outcome::success(); }

auto finish_file_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting finish_file_t (visitor = {}), folder = {}, file = {}", (const void *)&visitor, folder_id,
              file_name);
    return visitor(*this, custom);
}
