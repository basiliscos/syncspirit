// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "update_contact.h"
#include "model/diff/cluster_visitor.h"
#include "model/cluster.h"
#include <fmt/core.h>
#include <set>

using namespace syncspirit;
using namespace syncspirit::model::diff::contact;

namespace {

struct comparator_t {
    bool operator()(const utils::uri_ptr_t &l, const utils::uri_ptr_t &r) const { return *l < *r; }
};
} // namespace

using set_t = std::set<utils::uri_ptr_t, comparator_t>;

update_contact_t::update_contact_t(const model::cluster_t &cluster, const model::device_id_t &device_,
                                   const utils::uri_container_t &uris_) noexcept
    : device{device_}, self{false} {
    auto &devices = cluster.get_devices();
    known = (bool)devices.by_sha256(device.get_sha256());
    if (known) {
        self = cluster.get_device()->device_id().get_sha256() == device.get_sha256();
    }
    LOG_DEBUG(log, "update_contact_t, device = {}, known = {}", device.get_short(), known);

    set_t set;
    std::copy(begin(uris_), end(uris_), std::inserter(set, set.begin()));

    this->uris.reserve(set.size());
    for (auto &url : set) {
        this->uris.emplace_back(url->clone());
    }
}

auto update_contact_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept
    -> outcome::result<void> {
    if (known) {
        auto &devices = cluster.get_devices();
        auto device = devices.by_sha256(this->device.get_sha256());
        auto uris_clone = utils::uri_container_t();
        uris_clone.reserve(uris.size());
        for (auto &url : uris) {
            uris_clone.emplace_back(url->clone());
        }
        device->assign_uris(std::move(uris_clone));
    }
    return applicator_t::apply_sibling(cluster, controller);
}

auto update_contact_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    return visitor(*this, custom);
}
