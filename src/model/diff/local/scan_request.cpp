// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2026 Ivan Baidakou

#include "scan_request.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::local;

scan_request_t::scan_request_t(std::string_view folder_id_, std::string_view sub_dir_)
    : folder_id{folder_id_}, sub_dir(sub_dir_) {
    LOG_DEBUG(log, "scan_request_t, folder = {}, sub_dir = {}", folder_id, sub_dir);
}

auto scan_request_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting scan_request_t");
    return visitor(*this, custom);
}
