// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "scan_finish.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::local;

scan_finish_t::scan_finish_t(std::string_view folder_id_, const pt::ptime &at_)
    : folder_id{std::move(folder_id_)}, at{at_} {}

auto scan_finish_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto folder = cluster.get_folders().by_id(folder_id);
    folder->set_scan_finish(at);
    auto r = applicator_t::apply_sibling(cluster);
    if (auto aug = folder->get_augmentation(); aug) {
        aug->on_update();
    }
    return r;
}

auto scan_finish_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting scan_finish_t");
    return visitor(*this, custom);
}
