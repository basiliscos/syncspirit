// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "scan_request.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::local;

scan_request_t::scan_request_t(std::string_view folder_id_) : folder_id{std::move(folder_id_)} {}

auto scan_request_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting scan_request_t");
    return visitor(*this, custom);
}
