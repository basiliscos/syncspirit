// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "remote_copy.h"
#include "../cluster_visitor.h"

using namespace syncspirit::model::diff::advance;

auto remote_copy_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting remote_copy_t, folder = {}, file = {}", folder_id, proto_file.name());
    return visitor(*this, custom);
}
